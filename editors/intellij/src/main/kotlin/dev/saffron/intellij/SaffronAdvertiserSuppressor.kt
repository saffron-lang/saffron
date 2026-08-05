package dev.saffron.intellij

import com.intellij.openapi.diagnostic.logger
import com.intellij.openapi.fileTypes.FileTypeFactory
import com.intellij.openapi.project.Project
import com.intellij.openapi.startup.ProjectActivity
import com.intellij.openapi.updateSettings.impl.pluginsAdvertisement.UnknownFeature
import com.intellij.openapi.updateSettings.impl.pluginsAdvertisement.UnknownFeaturesCollector

/**
 * Stops IntelliJ's Plugin Advertiser from offering to install a Saffron plugin
 * for `.sf` files when this plugin is already installed.
 *
 * WHY THE OBVIOUS FIX IS WRONG. The advertiser suggests a plugin for a file only
 * when the file's `FileType` is a `PlainTextLikeFileType` (or content-detected) —
 * verified in the platform bytecode, `PluginAdvertiserEditorNotificationProviderKt
 * .getSuggestionData`. Saffron `.sf` files are coloured by a TextMate bundle
 * (see [SaffronTextMateBundleProvider]), and `TextMateFileType` *is* a
 * `PlainTextLikeFileType`, so the advertiser fires. The tempting fix — register a
 * real `FileType` for `.sf` — backfires: `TextMateFileType.isMyFileType` only
 * claims a file whose registered type is `UnknownFileType`, `PlainTextFileType`,
 * or TextMate itself, so a custom `FileType` would take `.sf` away from TextMate
 * and **kill the syntax highlighting**. The two goals are in direct tension
 * through the FileType, and highlighting wins.
 *
 * WHAT THIS DOES INSTEAD. It performs, at startup, exactly the action the
 * advertiser itself takes when a user clicks "ignore extension": it adds the
 * `.sf` file-type feature to [UnknownFeaturesCollector]'s ignored set. That is a
 * per-project setting the advertiser consults before showing the banner, and it
 * never touches the FileType, so TextMate highlighting is preserved by
 * construction.
 *
 * MATCHING THE PLATFORM'S FEATURE. `UnknownFeature.equals` compares only
 * `featureType` and `implementationName` (platform bytecode). The advertiser
 * builds the `.sf` feature (in `StateKt.createUnknownExtensionFeature`) with
 * `featureType = FileTypeFactory.FILE_TYPE_FACTORY_EP.name` and
 * `implementationName` = the extension. We reconstruct it the same way — using
 * the EP-name property rather than the literal `"com.intellij.fileTypeFactory"`,
 * so a platform rename cannot silently desync the two. If the extension the
 * advertiser stores ever stops matching this, the banner returns (it does not
 * misfire in the other direction), which is the safe way to fail.
 *
 * ON `@ApiStatus.Internal`. `UnknownFeaturesCollector` is marked internal, so the
 * Plugin Verifier flags this call (it is Compatible, not an error). There is no
 * public API for "ignore this file-type suggestion", and the whole call is
 * wrapped so that if a future platform removes or reshapes it, the failure is a
 * single logged warning and the banner simply reappears — never a stack trace on
 * every project open. That is the same safe-failure direction as above.
 */
internal class SaffronAdvertiserSuppressor : ProjectActivity {
    override suspend fun execute(project: Project) {
        try {
            val featureType = FileTypeFactory.FILE_TYPE_FACTORY_EP.name
            val collector = UnknownFeaturesCollector.getInstance(project)
            for (ext in EXTENSIONS) {
                // implementationName is the extension; the display fields are
                // outside equals, so they are cosmetic here and describe the entry
                // in the ignored-features UI.
                collector.ignoreFeature(
                    UnknownFeature(featureType, "File Type", ext),
                )
            }
        } catch (t: Throwable) {
            // Internal API: degrade to "the banner shows" rather than failing the
            // whole startup activity. Not fatal — the plugin still works, the user
            // just sees a suggestion they can dismiss.
            LOG.warn("Could not suppress the plugin-advertiser suggestion for .sf", t)
        }
    }

    private companion object {
        val LOG = logger<SaffronAdvertiserSuppressor>()

        // The advertiser keys on the extension the way the file name presents it.
        // Only `.sf` today; a list so a second extension is a one-line change.
        val EXTENSIONS = listOf(".sf")
    }
}
