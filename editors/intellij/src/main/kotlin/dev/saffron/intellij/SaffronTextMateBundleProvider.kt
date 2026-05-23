package dev.saffron.intellij

import com.intellij.ide.plugins.PluginManagerCore
import com.intellij.openapi.extensions.PluginId
import org.jetbrains.plugins.textmate.api.TextMateBundleProvider

class SaffronTextMateBundleProvider : TextMateBundleProvider {
    override fun getBundles(): List<TextMateBundleProvider.PluginBundle> {
        val plugin = PluginManagerCore.getPlugin(PluginId.getId("dev.saffron.intellij"))
            ?: return emptyList()
        val bundlePath = plugin.pluginPath.resolve("textmate")
        if (!bundlePath.toFile().exists()) return emptyList()
        return listOf(TextMateBundleProvider.PluginBundle("Saffron", bundlePath))
    }
}
