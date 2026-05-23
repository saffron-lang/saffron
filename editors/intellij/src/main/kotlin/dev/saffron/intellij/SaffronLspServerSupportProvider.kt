package dev.saffron.intellij

import com.intellij.openapi.project.Project
import com.intellij.openapi.vfs.VirtualFile
import com.intellij.platform.lsp.api.LspServerSupportProvider
import com.intellij.platform.lsp.api.ProjectWideLspServerDescriptor

class SaffronLspServerSupportProvider : LspServerSupportProvider {
    override fun fileOpened(
        project: Project,
        file: VirtualFile,
        serverStarter: LspServerSupportProvider.LspServerStarter
    ) {
        if (file.extension == "sf") {
            serverStarter.ensureServerStarted(SaffronLspServerDescriptor(project))
        }
    }
}

class SaffronLspServerDescriptor(project: Project) : ProjectWideLspServerDescriptor(project, "Saffron") {
    override fun isSupportedFile(file: VirtualFile) = file.extension == "sf"

    override fun createCommandLine(): com.intellij.execution.configurations.GeneralCommandLine {
        val serverPath = findServerPath()
        return com.intellij.execution.configurations.GeneralCommandLine("node", serverPath, "--stdio")
    }

    private fun findServerPath(): String {
        val projectPath = project.basePath ?: ""
        val localServer = "$projectPath/editors/shared/out/server.js"
        if (java.io.File(localServer).exists()) return localServer

        val home = System.getProperty("user.home")
        val globalServer = "$home/.saffron/lsp/server.js"
        if (java.io.File(globalServer).exists()) return globalServer

        return localServer
    }
}
