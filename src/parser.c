#include "../inc/parser.h"

#include "../inc/debug.h"
#include "../inc/type.h"
#include "../inc/sym.h"
#include "../inc/ast.h"

#include "../inc/lexer.h"
#include "../inc/parser-helpers.h"
#include "../inc/parser-value.h"
#include "../inc/parser-decl.h"

#include "stdlib.h"
#include "string.h"

static ast* parserModule (parserCtx* ctx);
static ast* parserUsing (parserCtx* ctx);

static ast* parserLine (parserCtx* ctx);

static ast* parserIf (parserCtx* ctx);
static ast* parserWhile (parserCtx* ctx);
static ast* parserDoWhile (parserCtx* ctx);
static ast* parserFor (parserCtx* ctx);

static parserCtx parserInit (const char* filename, char* fullname, sym* global, vector/*<char*>*/* searchPaths) {
    lexerCtx* lexer = lexerInit(fopen(fullname, "r"));

    vectorPush(searchPaths, fgetpath(fullname));

    return (parserCtx) {lexer, {lexer->stream->line, lexer->stream->lineChar},
                        filename, fullname, searchPaths,
                        global,
                        0,
                        0, 0};
}

static void parserEnd (parserCtx ctx) {
    free(ctx.fullname);
    free(vectorPop(ctx.searchPaths));
    lexerEnd(ctx.lexer);
}

static char* parserFindFile (const char* filename, vector/*<char*>*/* searchPaths) {
    int filenameLength = strlen(filename);

    for (int i = 0; i < searchPaths->length; i++) {
        const char* path = vectorGet(searchPaths, i);
        char* fullname;

        if (path[0] != 0) {
            fullname = malloc(strlen(path)+1+filenameLength+1);
            sprintf(fullname, "%s/%s", path, filename);

        } else
            fullname = strdup(filename);

        if (fexists(fullname))
            return fullname;

        else
            free(fullname);
    }

    return 0;
}

parserResult parser (const char* filename, sym* global, vector/*<char*>*/* searchPaths) {
    char* fullname = parserFindFile(filename, searchPaths);

    if (fullname) {
        parserCtx ctx = parserInit(filename, fullname, global, searchPaths);
        ast* Module = parserModule(&ctx);
        parserEnd(ctx);

        return (parserResult) {Module, ctx.errors, ctx.warnings, false};

    } else
        return (parserResult) {astCreateInvalid((tokenLocation) {0, 0}),
                               1, 0, true};
}

/**
 * Module = [{ Decl# | Using | ";" }]
 */
static ast* parserModule (parserCtx* ctx) {
    debugEnter("Module");

    ast* Module = astCreate(astModule, ctx->location);
    Module->symbol = ctx->scope;

    while (ctx->lexer->token != tokenEOF) {
        if (tokenTryMatchPunct(ctx, punctSemicolon))
            astAddChild(Module, astCreateEmpty(ctx->location));

        else if (tokenIsKeyword(ctx, keywordUsing))
            astAddChild(Module, parserUsing(ctx));

        else
            astAddChild(Module, parserDecl(ctx, true));

        //debugWait();
    }

    debugLeave();

    return Module;
}

/**
 * Using = "using" <Str> ";"
 */
static ast* parserUsing (parserCtx* ctx) {
    debugEnter("Using");

    tokenLocation loc = ctx->location;
    tokenMatchKeyword(ctx, keywordUsing);

    char* name = tokenMatchStr(ctx);
    ast* Node = astCreateUsing(loc, name);

    if (name[0] != 0) {
        //!!DONT USE THIS SCOPE, HEADERS SHOULDNT INTERFERE!!
        parserResult res = parser(name, ctx->scope, ctx->searchPaths);

        ctx->errors += res.errors;
        ctx->warnings += res.warnings;
        Node->r = res.tree;

        if (res.notfound)
            errorFileNotFound(ctx, name);
    }

    tokenMatchPunct(ctx, punctSemicolon);

    debugLeave();

    return Node;
}

/**
 * Code = ( "{" [{ Line }] "}" ) | Line
 */
ast* parserCode (parserCtx* ctx) {
    debugEnter("Code");

    ast* Node = astCreate(astCode, ctx->location);
    Node->symbol = symCreateScope(ctx->scope);
    sym* OldScope = scopeSet(ctx, Node->symbol);

    if (tokenTryMatchPunct(ctx, punctLBrace))
        while (!tokenTryMatchPunct(ctx, punctRBrace) && ctx->lexer->token != tokenEOF)
            astAddChild(Node, parserLine(ctx));

    else
        astAddChild(Node, parserLine(ctx));

    ctx->scope = OldScope;

    debugLeave();

    return Node;
}

/**
 * Line = If | While | DoWhile | For | Code | Decl# | ( [ ( "return" Value ) | "break" | Value ] ";" )
 */
static ast* parserLine (parserCtx* ctx) {
    debugEnter("Line");

    ast* Node = 0;

    if (tokenIsKeyword(ctx, keywordIf))
        Node = parserIf(ctx);

    else if (tokenIsKeyword(ctx, keywordWhile))
        Node = parserWhile(ctx);

    else if (tokenIsKeyword(ctx, keywordDo))
        Node = parserDoWhile(ctx);

    else if (tokenIsKeyword(ctx, keywordFor))
        Node = parserFor(ctx);

    else if (tokenIsPunct(ctx, punctLBrace))
        Node = parserCode(ctx);

    else if (tokenIsDecl(ctx))
        Node = parserDecl(ctx, false);

    /*Statements (that which require ';')*/
    else {
        if (tokenTryMatchKeyword(ctx, keywordReturn)) {
            Node = astCreate(astReturn, ctx->location);

            if (!tokenIsPunct(ctx, punctSemicolon))
                Node->r = parserValue(ctx);

        } else if (tokenIsKeyword(ctx, keywordBreak)) {
            if (ctx->breakLevel == 0)
                errorIllegalOutside(ctx, "break", "a loop");

            else
                tokenMatch(ctx);

            Node = astCreate(astBreak, ctx->location);

        /*Allow empty lines, ";"*/
        } else if (tokenIsPunct(ctx, punctSemicolon))
            Node = astCreateEmpty(ctx->location);

        else
            Node = parserValue(ctx);

        tokenMatchPunct(ctx, punctSemicolon);
    }

    debugLeave();

    //debugWait();

    return Node;
}

/**
 * If = "if" "(" Value ")" Code [ "else" Code ]
 */
static ast* parserIf (parserCtx* ctx) {
    debugEnter("If");

    ast* Node = astCreate(astBranch, ctx->location);

    tokenMatchKeyword(ctx, keywordIf);
    tokenMatchPunct(ctx, punctLParen);
    astAddChild(Node, parserValue(ctx));
    tokenMatchPunct(ctx, punctRParen);

    Node->l = parserCode(ctx);

    if (tokenTryMatchKeyword(ctx, keywordElse))
        Node->r = parserCode(ctx);

    debugLeave();

    return Node;
}

/**
 * While = "while" "(" Value ")" Code
 */
static ast* parserWhile (parserCtx* ctx) {
    debugEnter("While");

    ast* Node = astCreate(astLoop, ctx->location);

    tokenMatchKeyword(ctx, keywordWhile);
    tokenMatchPunct(ctx, punctLParen);
    Node->l = parserValue(ctx);
    tokenMatchPunct(ctx, punctRParen);

    ctx->breakLevel++;
    Node->r = parserCode(ctx);
    ctx->breakLevel--;

    debugLeave();

    return Node;
}

/**
 * DoWhile = "do" Code "while" "(" Value ")" ";"
 */
static ast* parserDoWhile (parserCtx* ctx) {
    debugEnter("DoWhile");

    ast* Node = astCreate(astLoop, ctx->location);

    tokenMatchKeyword(ctx, keywordDo);

    ctx->breakLevel++;
    Node->l = parserCode(ctx);
    ctx->breakLevel--;

    tokenMatchKeyword(ctx, keywordWhile);
    tokenMatchPunct(ctx, punctLParen);
    Node->r = parserValue(ctx);
    tokenMatchPunct(ctx, punctRParen);
    tokenMatchPunct(ctx, punctSemicolon);

    debugLeave();

    return Node;
}

/**
 * For := "for" "(" Decl# | ( [ Value ] ";" ) [ Value ] ";" [ Value ] ")" Code
 */
static ast* parserFor (parserCtx* ctx) {
    debugEnter("For");

    ast* Node = astCreate(astIter, ctx->location);

    tokenMatchKeyword(ctx, keywordFor);
    tokenMatchPunct(ctx, punctLParen);

    Node->symbol = symCreateScope(ctx->scope);
    sym* OldScope = scopeSet(ctx, Node->symbol);

    /*Initializer*/
    if (tokenIsDecl(ctx))
        astAddChild(Node, parserDecl(ctx, false));

    else {
        if (!tokenIsPunct(ctx, punctSemicolon))
            astAddChild(Node, parserValue(ctx));

        else
            astAddChild(Node, astCreateEmpty(ctx->location));

        tokenMatchPunct(ctx, punctSemicolon);
    }

    /*Condition*/
    if (!tokenIsPunct(ctx, punctSemicolon))
        astAddChild(Node, parserValue(ctx));

    else
        astAddChild(Node, astCreateEmpty(ctx->location));

    tokenMatchPunct(ctx, punctSemicolon);

    /*Iterator*/
    if (!tokenIsPunct(ctx, punctRParen))
        astAddChild(Node, parserValue(ctx));

    else
        astAddChild(Node, astCreateEmpty(ctx->location));

    tokenMatchPunct(ctx, punctRParen);

    ctx->breakLevel++;
    Node->l = parserCode(ctx);
    ctx->breakLevel--;

    ctx->scope = OldScope;

    debugLeave();

    return Node;
}
