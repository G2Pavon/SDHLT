#include <sys/stat.h>

#include "hlrad.h"
#include "blockmem.h"
#include "log.h"

/*
 * =============
 * writetransfers
 * =============
 */

void writetransfers(const char *const transferfile, const long total_patches)
{
    auto *file = fopen(transferfile, "w+b");
    if (file != nullptr)
    {
        Patch *patch;

        Log("Writing transfers file [%s]\n", transferfile);

        unsigned amtwritten = fwrite(&total_patches, sizeof(total_patches), 1, file);
        if (amtwritten != 1)
        {
            goto FailedWrite;
        }

        long patchcount = total_patches;
        for (patch = g_patches; patchcount-- > 0; patch++)
        {
            amtwritten = fwrite(&patch->iIndex, sizeof(patch->iIndex), 1, file);
            if (amtwritten != 1)
            {
                goto FailedWrite;
            }

            if (patch->iIndex)
            {
                amtwritten = fwrite(patch->tIndex, sizeof(TransferIndex), patch->iIndex, file);
                if (amtwritten != patch->iIndex)
                {
                    goto FailedWrite;
                }
            }

            amtwritten = fwrite(&patch->iData, sizeof(patch->iData), 1, file);
            if (amtwritten != 1)
            {
                goto FailedWrite;
            }
            if (patch->iData)
            {
                amtwritten = fwrite(patch->tData, float_size[g_transfer_compress_type], patch->iData, file);
                if (amtwritten != patch->iData)
                {
                    goto FailedWrite;
                }
            }
        }

        fclose(file);
    }
    else
    {
        Error("Failed to open incremenetal file [%s] for writing\n", transferfile);
    }
    return;

FailedWrite:
    fclose(file);
    unlink(transferfile);
    // Warning("Failed to generate incremental file [%s] (probably ran out of disk space)\n");
    Warning("Failed to generate incremental file [%s] (probably ran out of disk space)\n", transferfile); //--vluzacn
}

/*
 * =============
 * readtransfers
 * =============
 */

auto readtransfers(const char *const transferfile, const long numpatches) -> bool
{
    long total_patches;

    auto *file = fopen(transferfile, "rb");
    if (file != nullptr)
    {
        Patch *patch;

        Log("Reading transfers file [%s]\n", transferfile);

        unsigned amtread = fread(&total_patches, sizeof(total_patches), 1, file);
        if (amtread != 1)
        {
            goto FailedRead;
        }
        if (total_patches != numpatches)
        {
            goto FailedRead;
        }

        long patchcount = total_patches;
        for (patch = g_patches; patchcount-- > 0; patch++)
        {
            amtread = fread(&patch->iIndex, sizeof(patch->iIndex), 1, file);
            if (amtread != 1)
            {
                goto FailedRead;
            }
            if (patch->iIndex)
            {
                patch->tIndex = (TransferIndex *)AllocBlock(patch->iIndex * sizeof(TransferIndex *));
                hlassume(patch->tIndex != nullptr, assume_NoMemory);
                amtread = fread(patch->tIndex, sizeof(TransferIndex), patch->iIndex, file);
                if (amtread != patch->iIndex)
                {
                    goto FailedRead;
                }
            }

            amtread = fread(&patch->iData, sizeof(patch->iData), 1, file);
            if (amtread != 1)
            {
                goto FailedRead;
            }
            if (patch->iData)
            {
                patch->tData = (transfer_data_t *)AllocBlock(patch->iData * float_size[g_transfer_compress_type] + unused_size);
                hlassume(patch->tData != nullptr, assume_NoMemory);
                amtread = fread(patch->tData, float_size[g_transfer_compress_type], patch->iData, file);
                if (amtread != patch->iData)
                {
                    goto FailedRead;
                }
            }
        }

        fclose(file);
        // Warning("Finished reading transfers file [%s] %d\n", transferfile);
        Warning("Finished reading transfers file [%s]\n", transferfile); //--vluzacn
        return true;
    }
    Warning("Failed to open transfers file [%s]\n", transferfile);
    return false;

FailedRead:
{
    Patch *patch = g_patches;

    for (unsigned x = 0; x < g_num_patches; x++, patch++)
    {
        FreeBlock(patch->tData);
        FreeBlock(patch->tIndex);
        patch->iData = 0;
        patch->iIndex = 0;
        patch->tData = nullptr;
        patch->tIndex = nullptr;
    }
}
    fclose(file);
    unlink(transferfile);
    return false;
}
