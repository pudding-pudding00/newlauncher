/* newlauncher (M16, Spec v2 9.2): Läufe einer NTFS-Datei für die Fragmentliste. Eigener Code, keine
 * Upstream-Datei; liegt neben libntfs, weil er dessen interne Header braucht. Lizenz wie das Projekt (GPLv3). */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t vcn;    /* Cluster in der Datei */
    uint64_t lcn;    /* Cluster in der Partition; 0 bei hole */
    uint64_t length; /* Cluster */
    int hole;        /* Lücke (sparse): liegt nicht auf dem Gerät */
} nl_ntfs_run;

typedef struct {
    uint32_t clusterBytes;
    uint64_t size; /* Dateigröße in Byte */
} nl_ntfs_file_info;

enum {
    NL_NTFS_OK = 0,
    NL_NTFS_ERR_NOVOL = -1,       /* kein eingehängtes NTFS-Volume unter diesem Gerätenamen */
    NL_NTFS_ERR_NOTFOUND = -2,    /* Datei oder Datenstrom fehlt */
    NL_NTFS_ERR_UNSUPPORTED = -3, /* resident, komprimiert, sparse, verschlüsselt oder nicht initialisiert */
    NL_NTFS_ERR_IO = -4           /* Runlist nicht lesbar */
};

/* path mit Gerät ("usb2:/wii/a.iso"). Ruft cb je Lauf in Dateireihenfolge, unter der Volume-Sperre. */
int nl_ntfs_file_runs(const char* path, nl_ntfs_file_info* info, void (*cb)(void* ctx, const nl_ntfs_run* run),
                      void* ctx);

#ifdef __cplusplus
}
#endif
