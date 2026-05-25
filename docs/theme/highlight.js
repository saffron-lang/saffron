/*
 * Saffron language definition for highlight.js
 * Registered as both "saffron" and "sf"
 */
hljs.registerLanguage("saffron", function(hljs) {
    var KEYWORDS = {
        keyword: [
            "var", "let", "fun", "return", "if", "else", "while", "for", "in",
            "class", "extends", "enum", "match", "import", "as", "from",
            "and", "or", "is", "this", "super", "throw", "try", "catch",
            "finally", "break", "continue", "yield", "interface", "type",
            "dataclass", "resume"
        ],
        literal: ["true", "false", "nil"],
        built_in: ["IO", "Task", "Async"]
    };

    var STRING = {
        className: "string",
        variants: [
            {
                begin: '"',
                end: '"',
                contains: [
                    hljs.BACKSLASH_ESCAPE,
                    {
                        className: "subst",
                        begin: "\\$\\{",
                        end: "\\}",
                        contains: [hljs.APOS_STRING_MODE, hljs.C_NUMBER_MODE]
                    }
                ]
            }
        ]
    };

    var NUMBER = {
        className: "number",
        variants: [
            { begin: "\\b0[xX][0-9a-fA-F]+" },
            { begin: "\\b\\d+\\.\\d+" },
            { begin: "\\b\\d+" }
        ]
    };

    var COMMENT = {
        className: "comment",
        variants: [
            { begin: "///", end: "$", className: "doctag" },
            { begin: "//!", end: "$", className: "doctag" },
            { begin: "//", end: "$" }
        ]
    };

    var TYPE = {
        className: "type",
        begin: "\\b[A-Z][a-zA-Z0-9_]*\\b"
    };

    var FUNCTION_DEF = {
        className: "function",
        beginKeywords: "fun",
        end: "\\(",
        excludeEnd: true,
        contains: [
            {
                className: "title",
                begin: "[a-zA-Z_][a-zA-Z0-9_?]*"
            }
        ]
    };

    var CLASS_DEF = {
        className: "class",
        beginKeywords: "class enum interface",
        end: "\\{",
        excludeEnd: true,
        contains: [
            {
                className: "title",
                begin: "[A-Z][a-zA-Z0-9_]*"
            }
        ]
    };

    var GENERIC = {
        className: "type",
        begin: "<",
        end: ">",
        contains: [
            { className: "type", begin: "[A-Z][a-zA-Z0-9_]*" },
            { begin: ",\\s*" }
        ]
    };

    return {
        name: "Saffron",
        aliases: ["sf"],
        keywords: KEYWORDS,
        contains: [
            COMMENT,
            STRING,
            NUMBER,
            FUNCTION_DEF,
            CLASS_DEF,
            GENERIC,
            TYPE,
            {
                className: "operator",
                begin: "\\|>|=>|->|\\.\\.\\."
            }
        ]
    };
});

// Re-highlight saffron code blocks that were missed on first pass
// (book.js runs highlightBlock before this language was registered)
document.querySelectorAll('code.language-saffron, code.language-sf').forEach(function(block) {
    block.removeAttribute('data-highlighted');
    block.classList.remove('hljs');
    block.innerHTML = block.textContent;
    hljs.highlightElement(block);
});
