package dev.saffron.intellij

import com.intellij.openapi.fileTypes.LanguageFileType
import javax.swing.Icon

object SaffronFileType : LanguageFileType(SaffronLanguage) {
    override fun getName() = "Saffron"
    override fun getDescription() = "Saffron language file"
    override fun getDefaultExtension() = "sf"
    override fun getIcon(): Icon? = null
}
