package dev.saffron.intellij

import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.platform.lsp.api.LspServerSupportProvider
import com.intellij.platform.lsp.api.ProjectWideLspServerDescriptor
import com.intellij.platform.lsp.api.LspServerSupportProvider.LspServerStarter
import com.intellij.platform.lsp.api.customization.LspFormattingSupport

internal class SaffronLspServerSupportProvider : LspServerSupportProvider {
    override fun fileOpened(
        project: Project,
        file: VirtualFile,
        serverStarter: LspServerStarter
    ) {
        if (file.extension == "sf") {
            serverStarter.ensureServerStarted(SaffronLspServerDescriptor(project))
        }
    }
}

private class SaffronLspServerDescriptor(project: Project) :
    ProjectWideLspServerDescriptor(project, "Saffron") {

    override fun isSupportedFile(file: VirtualFile) = file.extension == "sf"

    /**
     * Feature opt-ins.
     *
     * The platform's default `LspCustomization` already enables everything the
     * server advertises -- diagnostics, hover, go-to-definition, completion,
     * semantic tokens, find-references, document highlights, folding, signature
     * help, document and workspace symbols -- gated on the server's own
     * capability flags, so those need no override here.
     *
     * Formatting is the exception. `LspFormattingSupport` only formats a file
     * "exclusively by server" when there is no IntelliJ formatter for it, and
     * since Saffron files are backed by a TextMate bundle rather than a real PSI
     * language, IntelliJ believes it has its own formatter and reformatting a .sf
     * file does whitespace guesswork instead of calling saffronc. Overriding it
     * to true routes Reformat Code (and format-on-save) through the LSP, which is
     * the only formatter that actually knows the language.
     *
     * Rename is NOT available: the IntelliJ LSP client has no rename support at
     * all in 2025.3 -- `textDocument/rename` appears only in its capability
     * bookkeeping, never in a request. The server implements it for VS Code; in
     * IntelliJ, Refactor > Rename falls back to the TextMate word-occurrence
     * rename, which is not scope-aware. Nothing here can change that.
     */
    override val lspFormattingSupport = object : LspFormattingSupport() {
        override fun shouldFormatThisFileExclusivelyByServer(
            file: VirtualFile,
            ideCanFormatThisFileItself: Boolean,
            serverExplicitlyWantsToFormatThisFile: Boolean
        ): Boolean = file.extension == "sf"
    }

    override fun createCommandLine(): GeneralCommandLine {
        val nodePath = findNode()
        val serverPath = findServerPath()
        return GeneralCommandLine(nodePath, serverPath, "--stdio")
            .withWorkDirectory(project.basePath)
    }

    private fun findNode(): String {
        val candidates = buildList {
            newestNvmNode()?.let { add(it) }
            add("/opt/homebrew/bin/node")
            add("/usr/local/bin/node")
        }
        // Falling back to the bare name lets PATH resolve it, though the IDE is
        // usually launched from Finder with a login shell's PATH missing.
        return candidates.firstOrNull { java.io.File(it).canExecute() } ?: "node"
    }

    /**
     * The newest node under nvm, or null if nvm is not installed.
     *
     * Versions are compared numerically, per component. Sorting the directory
     * names as strings puts v22.9.0 above v22.22.0 (and v6 above both), so the
     * previous version of this picked an arbitrary install — which on a machine
     * with an old node still around meant launching the server on it and failing
     * on modern syntax, with no message pointing at the node version.
     */
    private fun newestNvmNode(): String? {
        val nvmDir = java.io.File(System.getenv("HOME") ?: return null, ".nvm/versions/node")
        val versions = nvmDir.listFiles { f: java.io.File -> f.isDirectory } ?: return null
        return versions
            .mapNotNull { dir ->
                val parts = dir.name.removePrefix("v").split(".")
                    .map { it.toIntOrNull() ?: return@mapNotNull null }
                if (parts.size < 3) null else parts to dir
            }
            .maxWithOrNull(compareBy({ it.first[0] }, { it.first[1] }, { it.first[2] }))
            ?.second
            ?.resolve("bin/node")
            ?.takeIf { it.canExecute() }
            ?.absolutePath
    }

    private fun findServerPath(): String {
        val projectPath = project.basePath ?: ""
        val localServer = "$projectPath/editors/shared/out/server.js"
        if (java.io.File(localServer).exists()) return localServer

        val home = System.getenv("HOME") ?: ""
        val globalServer = "$home/.saffron/lsp/server.js"
        if (java.io.File(globalServer).exists()) return globalServer

        // Returning the local path when it does not exist makes node exit with
        // "Cannot find module", which the IDE reports as the server crashing --
        // true but unhelpful, since the actual fix is to run editors/build.sh.
        throw com.intellij.execution.ExecutionException(
            "Saffron language server not found. Build it with editors/build.sh " +
                "(expected at $localServer) or install it to $globalServer."
        )
    }
}
