package dev.saffron.intellij

import com.intellij.execution.configurations.ConfigurationFactory
import com.intellij.execution.configurations.ConfigurationType
import com.intellij.execution.configurations.RunConfiguration
import com.intellij.openapi.project.Project

class SaffronPantryConfigurationFactory(type: ConfigurationType) : ConfigurationFactory(type) {
    override fun getId(): String = "SaffronPantryScript"

    override fun createTemplateConfiguration(project: Project): RunConfiguration =
        SaffronPantryRunConfiguration(project, this, "Pantry Script")
}
