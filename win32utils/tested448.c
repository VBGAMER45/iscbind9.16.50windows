#include <openssl/evp.h>
#include <openssl/objects.h>

int
main(void)
{
        EVP_PKEY_CTX *ctx;

        ctx = EVP_PKEY_CTX_new_id(NID_ED448, NULL);
        if (ctx == NULL)
                return (2);
        return (0);
}
