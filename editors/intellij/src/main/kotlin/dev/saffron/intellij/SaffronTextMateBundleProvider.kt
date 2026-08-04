package dev.saffron.intellij

import com.intellij.ide.plugins.PluginManager
import org.jetbrains.plugins.textmate.api.TextMateBundleProvider

class SaffronTextMateBundleProvider : TextMateBundleProvider {
    /**
     * The bundled Saffron TextMate grammar, or nothing if it is missing.
     *
     * The plugin is located by *this class* rather than by a hardcoded plugin
     * ID string, for two reasons, both of which were silent failures:
     *
     *  - A hardcoded ID has to be kept in sync with plugin.xml by hand, and
     *    when it drifts `getPlugin` returns null, this returns an empty list,
     *    and the only symptom is that .sf files lose all syntax highlighting:
     *    no error, no log line. That drift actually happened when the plugin ID
     *    was renamed to dev.saffron.lang. Asking which plugin owns this class
     *    cannot go stale.
     *
     *  - `PluginId.getId()` does not link before 2025.3. PluginId was a Java
     *    class in 2024.2 and is Kotlin now, so Kotlin compiles the call against
     *    the current platform as a `PluginId.Companion` getstatic, which throws
     *    NoSuchFieldError on an older IDE. The Plugin Verifier caught it
     *    against the sinceBuild floor; compilation cannot.
     *    `PluginManager.getPluginByClass` has the same signature in both.
     */
    override fun getBundles(): List<TextMateBundleProvider.PluginBundle> {
        val plugin = PluginManager.getPluginByClass(javaClass) ?: return emptyList()
        val bundlePath = plugin.pluginPath.resolve("textmate")
        if (!bundlePath.toFile().exists()) return emptyList()
        return listOf(TextMateBundleProvider.PluginBundle("Saffron", bundlePath))
    }
}
