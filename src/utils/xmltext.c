#include <string.h>
#include <stdio.h>
#include "utils/xmltext.h"

/* Finds needle (length needleLen) within [buf, buf+bufSize). Plain
 * memmem-alike since newlib doesn't provide memmem. */
static const char *findMem(const char *buf, size_t bufSize, const char *needle, size_t needleLen)
{
    if (needleLen == 0 || needleLen > bufSize)
        return NULL;
    const char *end = buf + bufSize - needleLen;
    for (const char *p = buf; p <= end; p++)
    {
        if (memcmp(p, needle, needleLen) == 0)
            return p;
    }
    return NULL;
}

static void unescapeInto(const char *src, size_t srcLen, char *out, size_t outSize)
{
    size_t o = 0;
    size_t i = 0;
    while (i < srcLen && o < outSize - 1)
    {
        if (src[i] == '&')
        {
            struct
            {
                const char *entity;
                char value;
            } entities[] = {
                {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
            int matched = 0;
            for (size_t e = 0; e < sizeof(entities) / sizeof(entities[0]); e++)
            {
                size_t elen = strlen(entities[e].entity);
                if (i + elen <= srcLen && memcmp(src + i, entities[e].entity, elen) == 0)
                {
                    out[o++] = entities[e].value;
                    i += elen;
                    matched = 1;
                    break;
                }
            }
            if (!matched)
                out[o++] = src[i++];
        }
        else
        {
            out[o++] = src[i++];
        }
    }
    out[o] = 0;
}

void xmlGetElementText(const char *buf, size_t bufSize, const char *elementName,
                        char *out, size_t outSize)
{
    out[0] = 0;
    if (outSize == 0)
        return;

    char openTag[80];
    int openTagLen = snprintf(openTag, sizeof(openTag), "<%s", elementName);
    if (openTagLen <= 0 || (size_t)openTagLen >= sizeof(openTag))
        return;

    char closeTag[80];
    int closeTagLen = snprintf(closeTag, sizeof(closeTag), "</%s>", elementName);
    if (closeTagLen <= 0 || (size_t)closeTagLen >= sizeof(closeTag))
        return;

    const char *bufEnd = buf + bufSize;
    const char *search = buf;

    while (search < bufEnd)
    {
        const char *tagStart = findMem(search, (size_t)(bufEnd - search), openTag, (size_t)openTagLen);
        if (!tagStart)
            return;

        /* Reject matches where elementName is only a prefix of a longer tag
         * name, e.g. "<longname_en>" must not match a search for "<longname>". */
        char afterName = (tagStart + openTagLen < bufEnd) ? tagStart[openTagLen] : '\0';
        if (afterName != '>' && afterName != ' ' && afterName != '\t' && afterName != '/' && afterName != '\n' && afterName != '\r')
        {
            search = tagStart + openTagLen;
            continue;
        }

        const char *gt = findMem(tagStart, (size_t)(bufEnd - tagStart), ">", 1);
        if (!gt)
            return;

        if (gt[-1] == '/')
        {
            /* Self-closing tag, no text content. */
            out[0] = 0;
            return;
        }

        const char *contentStart = gt + 1;
        const char *closeStart = findMem(contentStart, (size_t)(bufEnd - contentStart), closeTag, (size_t)closeTagLen);
        if (!closeStart)
            return;

        unescapeInto(contentStart, (size_t)(closeStart - contentStart), out, outSize);
        return;
    }
}
