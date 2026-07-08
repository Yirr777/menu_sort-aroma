#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct nlist
    {
        char *name;
        char *defn;
        struct nlist *next;
    } nlist;

    struct nlist *lookup(char *s);
    struct nlist *install(char *name, char *defn);

#ifdef __cplusplus
}
#endif
