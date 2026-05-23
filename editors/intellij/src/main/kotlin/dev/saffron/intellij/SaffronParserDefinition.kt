package dev.saffron.intellij

import com.intellij.lang.ASTNode
import com.intellij.lang.ParserDefinition
import com.intellij.lang.PsiParser
import com.intellij.lexer.EmptyLexer
import com.intellij.lexer.Lexer
import com.intellij.openapi.project.Project
import com.intellij.psi.FileViewProvider
import com.intellij.psi.PsiElement
import com.intellij.psi.PsiFile
import com.intellij.psi.impl.source.PsiPlainTextFileImpl
import com.intellij.psi.tree.IFileElementType
import com.intellij.psi.tree.TokenSet

class SaffronParserDefinition : ParserDefinition {
    companion object {
        val FILE = IFileElementType(SaffronLanguage)
    }

    override fun createLexer(project: Project?): Lexer = EmptyLexer()
    override fun createParser(project: Project?): PsiParser {
        throw UnsupportedOperationException("Not needed for LSP-only plugin")
    }
    override fun getFileNodeType(): IFileElementType = FILE
    override fun getCommentTokens(): TokenSet = TokenSet.EMPTY
    override fun getStringLiteralElements(): TokenSet = TokenSet.EMPTY
    override fun createElement(node: ASTNode?): PsiElement {
        throw UnsupportedOperationException("Not needed for LSP-only plugin")
    }
    override fun createFile(viewProvider: FileViewProvider): PsiFile {
        return PsiPlainTextFileImpl(viewProvider)
    }
}
