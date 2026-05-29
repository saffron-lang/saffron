package dev.saffron.intellij

import com.intellij.execution.configurations.ConfigurationFactory
import com.intellij.execution.configurations.ConfigurationType
import com.intellij.icons.AllIcons
import javax.swing.Icon

class SaffronPantryConfigurationType : ConfigurationType {
    override fun getDisplayName(): String = "Pantry Script"
    override fun getConfigurationTypeDescription(): String = "Run a pantry script from pantry.toml"
    override fun getIcon(): Icon = AllIcons.Actions.Execute
    override fun getId(): String = "SaffronPantryScript"
    override fun getConfigurationFactories(): Array<ConfigurationFactory> =
        arrayOf(SaffronPantryConfigurationFactory(this))
}
