package dev.saffron.intellij

import com.intellij.execution.configurations.GeneralCommandLine
import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.platform.lsp.api.LspServerSupportProvider
import com.intellij.platform.lsp.api.ProjectWideLspServerDescriptor
import com.intellij.platform.lsp.api.LspServerSupportProvider.LspServerStarter

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

    override fun createCommandLine(): GeneralCommandLine {
        val nodePath = findNode()
        val serverPath = findServerPath()
        return GeneralCommandLine(nodePath, serverPath, "--stdio")
            .withWorkDirectory(project.basePath)
    }

    private fun findNode(): String {
        val candidates = listOf(
            System.getenv("HOME") + "/.nvm/versions/node/" + findNvmVersion() + "/bin/node",
            "/opt/homebrew/bin/node",
            "/usr/local/bin/node",
            "node"
        )
        return candidates.firstOrNull { java.io.File(it).exists() } ?: "node"
    }

    private fun findNvmVersion(): String {
        val nvmDir = java.io.File(System.getenv("HOME") + "/.nvm/versions/node")
        if (!nvmDir.exists()) return ""
        val versions = nvmDir.listFiles()?.sortedDescending() ?: return ""
        return versions.firstOrNull()?.name ?: ""
    }

    private fun findServerPath(): String {
        val projectPath = project.basePath ?: ""
        val localServer = "$projectPath/editors/shared/out/server.js"
        if (java.io.File(localServer).exists()) return localServer

        val home = System.getenv("HOME") ?: ""
        val globalServer = "$home/.saffron/lsp/server.js"
        if (java.io.File(globalServer).exists()) return globalServer

        return localServer
    }
}
