package dev.saffron.intellij

import com.intellij.execution.DefaultExecutionResult
import com.intellij.execution.Executor
import com.intellij.execution.configurations.*
import com.intellij.execution.process.ProcessHandlerFactory
import com.intellij.execution.process.ProcessTerminatedListener
import com.intellij.execution.runners.ExecutionEnvironment
import com.intellij.openapi.options.SettingsEditor
import com.intellij.openapi.project.Project
import org.jdom.Element
import javax.swing.*
import java.awt.BorderLayout
import java.io.File

class SaffronPantryRunConfiguration(
    project: Project,
    factory: ConfigurationFactory,
    name: String
) : RunConfigurationBase<RunConfigurationOptions>(project, factory, name) {

    var scriptName: String = ""

    override fun readExternal(element: Element) {
        super.readExternal(element)
        scriptName = element.getAttributeValue("scriptName") ?: ""
    }

    override fun writeExternal(element: Element) {
        super.writeExternal(element)
        element.setAttribute("scriptName", scriptName)
    }

    override fun getConfigurationEditor(): SettingsEditor<out RunConfiguration> =
        SaffronPantrySettingsEditor(project)

    override fun getState(executor: Executor, environment: ExecutionEnvironment): RunProfileState {
        return RunProfileState { _, _ ->
            val workDir = project.basePath ?: "."
            val cmd = buildCommandLine(workDir)
            val handler = ProcessHandlerFactory.getInstance().createColoredProcessHandler(cmd)
            ProcessTerminatedListener.attach(handler)
            DefaultExecutionResult(null, handler)
        }
    }

    private fun buildCommandLine(workDir: String): GeneralCommandLine {
        val pantryBin = File(workDir, "build/pantry")
        if (pantryBin.exists() && pantryBin.canExecute()) {
            return GeneralCommandLine(pantryBin.absolutePath, "run", scriptName)
                .withWorkDirectory(workDir)
                .withParentEnvironmentType(GeneralCommandLine.ParentEnvironmentType.CONSOLE)
        }

        val rawCmd = resolveScriptCommand(workDir, scriptName)
        if (rawCmd != null) {
            return GeneralCommandLine("/bin/sh", "-c", rawCmd)
                .withWorkDirectory(workDir)
                .withParentEnvironmentType(GeneralCommandLine.ParentEnvironmentType.CONSOLE)
        }

        return GeneralCommandLine("pantry", "run", scriptName)
            .withWorkDirectory(workDir)
            .withParentEnvironmentType(GeneralCommandLine.ParentEnvironmentType.CONSOLE)
    }

    private fun resolveScriptCommand(workDir: String, name: String): String? {
        val tomlFile = File(workDir, "pantry.toml")
        if (!tomlFile.exists()) return null
        val scriptRegex = Regex("""^${Regex.escape(name)}\s*=\s*"(.+)"""")
        var inScripts = false
        for (line in tomlFile.readLines()) {
            if (line.trim() == "[scripts]") { inScripts = true; continue }
            if (line.trim().startsWith("[") && inScripts) break
            if (inScripts) {
                scriptRegex.find(line.trim())?.let { return it.groupValues[1] }
            }
        }
        return null
    }
}

class SaffronPantrySettingsEditor(private val project: Project) :
    SettingsEditor<SaffronPantryRunConfiguration>() {

    private val scriptField = JComboBox<String>()

    init {
        scriptField.isEditable = true
        loadScripts()
    }

    private fun loadScripts() {
        val basePath = project.basePath ?: return
        val tomlFile = File(basePath, "pantry.toml")
        if (!tomlFile.exists()) return

        val scriptRegex = Regex("""^(\w[\w-]*)\s*=""")
        var inScripts = false
        for (line in tomlFile.readLines()) {
            if (line.trim() == "[scripts]") { inScripts = true; continue }
            if (line.trim().startsWith("[") && inScripts) break
            if (inScripts) {
                scriptRegex.find(line.trim())?.let { match ->
                    scriptField.addItem(match.groupValues[1])
                }
            }
        }
    }

    override fun resetEditorFrom(config: SaffronPantryRunConfiguration) {
        scriptField.selectedItem = config.scriptName
    }

    override fun applyEditorTo(config: SaffronPantryRunConfiguration) {
        config.scriptName = scriptField.selectedItem?.toString() ?: ""
    }

    override fun createEditor(): JComponent {
        val panel = JPanel(BorderLayout(8, 0))
        panel.add(JLabel("Script:"), BorderLayout.WEST)
        panel.add(scriptField, BorderLayout.CENTER)
        return panel
    }
}
