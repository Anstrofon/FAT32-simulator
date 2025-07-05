#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <wchar.h>

#define MAX_FAT_SIZE_BYTES (4 * 1024 * 1024)

struct FAT32_BPB // SECTOR 0
{
    char jmp[3];
    char oem[8];                               // OC label
    unsigned short bytes_per_sector;          // size sector (e.g. 512 bytes)
    unsigned char sectors_per_cluster;       // number of sectors per cluster (e.g.8 => 8 * 512 = 4096 bytes (4KB) size of cluster)
    unsigned short reserved_sectors;        // size of reserved sector of FAT table: adress of FAT table (32 * 512 as 0x20 * 0x200 = 0x4000)
    unsigned char fat_amount;              // amount of copies of FAT table (if someone is corrupted, we can use another one)
    unsigned short root_dir_entries;      //  number of entries in root directory (number of clusters in data block)
    unsigned short total_sectors_16;

    unsigned char media_descriptor;      // type of media 0xF8 hard disk (0xF0 = floppy)
    //unsigned short sectors_per_fat;     //  size of FAT32 table; e.g.(8 * 512) * 512 = 1,8MB
    unsigned short fat16_size; // 0
    unsigned short sectors_per_track;
    unsigned short heads;              // number of heads
    unsigned int hidden_sectors;      // number of sector before the disk

    unsigned int total_sectors;      // size of disk; e.g. 15118440 sectors * 512 = 7,2GB
    unsigned int fat32_size;

    unsigned short flags;
    unsigned short version;
    unsigned int root_cluster;
    unsigned short sector_FS_info;
    unsigned short sector_backup_boot;
    unsigned char reserved[12];
    unsigned char drive_number;   // number of drive
    unsigned char reserved2;
    unsigned char boot_signature; // 0x29
    unsigned int volume_id;
    char volume_label[11];
    char file_system[8];

    unsigned char unused[420]; // if it was boot device here should be a code to be executed.
    unsigned char signature[2]; // 0x55, 0xAA
};

struct LFNentry
{
    uint8_t sequence_number;       // sequence number
    uint16_t name1[5];             // (Unicode)
    uint8_t attributes;            // 0x0F
    uint8_t entry_type;           // 0x00
    uint8_t checksum;             // SFN checksum
    uint16_t name2[6];
    uint16_t first_cluster;        // 0x0000
    uint16_t name3[2];
};

struct SFNentry
{
    unsigned char name[8];
    unsigned char ext[3];
    unsigned char attributes;
    unsigned char reserved;
    unsigned short time_created;
    unsigned short date_created;
    unsigned short date_last_accessed;
    unsigned short cluster_high;
    unsigned short time_last_modified;
    unsigned short date_last_modified;
    unsigned short cluster_low;
    unsigned int size;
};

int find_free_entry_offset(uint8_t* cluster, int cluster_size) {
    for (int offset = 0; offset < cluster_size; offset += 32) {
        if (cluster[offset] == 0x00 || cluster[offset] == 0xE5) {
            return offset;
        }
    }
    return -1; // no free slot
}

unsigned char sfn_checksum(const unsigned char *sfn)
{
    unsigned char sum = 0;
    for (int i = 0; i < 11; i++)
    {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + sfn[i];
    }
    return sum;
}

void create_folder(char* name, uint8_t* cluster, unsigned int id_cluster, int cluster_size)
{
    struct SFNentry folder;

    struct LFNentry lfn;

    int entry_offset = find_free_entry_offset(cluster, cluster_size);
    if (entry_offset < 0)
    {
        printf("No free directory entries!\n");
        return;
    }
    if(strlen(name) <= 8 )
    {
        memcpy(folder.name, name, strlen(name) + 1);
        memcpy(folder.ext, "   ", 3);
        folder.attributes = 0x10;
        folder.cluster_high = (id_cluster >> 16) & 0xFFFF;
        folder.cluster_low = id_cluster & 0xFFFF;
        folder.size = 0;

        // SFN
        memcpy(&cluster[entry_offset], &folder, sizeof(folder));

    }
    else
    {
        unsigned char checksum = sfn_checksum((uint8_t *)&folder);
        lfn.attributes = 0x0F;
        lfn.entry_type = 0;
        lfn.checksum = checksum;
        lfn.first_cluster = 0;


        uint16_t *unicode_name = calloc(strlen(name), sizeof(uint16_t));
        mbstowcs((wchar_t *)unicode_name, name, strlen(name));

        int total_chars = wcslen((wchar_t *)unicode_name);
        int entries_needed = (total_chars + 12) / 13;

        for (int i = 0; i < entries_needed; i++) {
            struct LFNentry lfn = {0};

            lfn.sequence_number = (uint8_t)(entries_needed - i);
            if (i == 0) lfn.sequence_number |= 0x40; // last

            lfn.attributes = 0x0F;
            lfn.entry_type = 0;
            lfn.checksum = checksum;
            lfn.first_cluster = 0;

            // 13 symbols in 3 parts
            for (int j = 0; j < 5; j++)
                lfn.name1[j] = (i * 13 + j < total_chars) ? unicode_name[i * 13 + j] : 0xFFFF;

            for (int j = 0; j < 6; j++)
                lfn.name2[j] = (i * 13 + 5 + j < total_chars) ? unicode_name[i * 13 + 5 + j] : 0xFFFF;

            for (int j = 0; j < 2; j++)
                lfn.name3[j] = (i * 13 + 11 + j < total_chars) ? unicode_name[i * 13 + 11 + j] : 0xFFFF;

            int lfn_total_size = entries_needed * 32;
            entry_offset -= lfn_total_size; // LFN м

            // check
            for (int i = 0; i < entries_needed; i++) {
                memcpy(&cluster[entry_offset + i * 32], &lfn, sizeof(lfn));
            }
        }

        free(unicode_name);
        // SFN after LFN
        memcpy(&cluster[entries_needed * 32], &folder, sizeof(folder));
    }
}

void to_format(struct FAT32_BPB* bpb, long size_file)
{
    memset(bpb, 0, sizeof(struct FAT32_BPB));
    bpb->jmp[0] = 0xEB;
    bpb->jmp[1] = 0x3C;
    bpb->jmp[2] = 0x90;

    bpb->oem[0] = 'F';
    bpb->oem[1] = 'A';

    bpb->bytes_per_sector = 512;
    bpb->sectors_per_cluster = 8;
    bpb->reserved_sectors = 32;
    bpb->fat_amount = 2;
    bpb->root_dir_entries = 0;
    bpb->total_sectors_16 = 0;
    bpb->media_descriptor = 0xF8;
    bpb->fat16_size = 0;
    bpb->sectors_per_track = 63;
    bpb->heads = 255;
    bpb->hidden_sectors = 0;
    bpb->total_sectors = size_file / 512;

    // size of FAT32 table;
    bpb->fat32_size = 1;
    int data_sectors, clusters;
    int required_fat_sectors;
    while (1)
    {
        data_sectors =  bpb->total_sectors - bpb->reserved_sectors - (bpb->fat_amount * bpb->fat32_size);
        clusters = data_sectors / bpb->sectors_per_cluster;
        required_fat_sectors = (clusters * 4 + bpb->bytes_per_sector - 1) / bpb->bytes_per_sector;

        if (required_fat_sectors <= bpb->fat32_size)
            break;

        bpb->fat32_size++;
    }


    bpb->flags =  0x0000;
    bpb->version =  0x0000;
    bpb->root_cluster = 2;
    bpb->sector_FS_info = 1;
    bpb->sector_backup_boot = 63;
    bpb->drive_number = 0x80;
    bpb->boot_signature = 0x29;
    bpb->volume_id = 0x12345678;

    bpb->volume_label[0] = 'F';
    bpb->volume_label[1] = 'A';
    bpb->volume_label[2] = 'T';
    bpb->volume_label[3] = '3';
    bpb->volume_label[4] = '2';
    bpb->volume_label[5] = 'I';
    bpb->volume_label[6] = 'M';
    bpb->volume_label[7] = 'G';
    bpb->volume_label[8] = '\0';
    bpb->file_system[0] = 'F';
    bpb->file_system[1] = 'A';
    bpb->file_system[2] = 'T';
    bpb->file_system[3] = '3';
    bpb->file_system[4] = '2';

    bpb->signature[0] = 0x55;
    bpb->signature[1] = 0xAA;
}

void init_root_directory(uint8_t *cluster_data, uint32_t self_cluster, uint32_t parent_cluster)
{
    struct SFNentry dot = {0};
    struct SFNentry dotdot = {0};

    // "."
    memset(dot.name, ' ', 8);
    dot.name[0] = '.';
    memset(dot.ext, ' ', 3);
    dot.attributes = 0x10; // directory
    dot.cluster_low = self_cluster & 0xFFFF;
    dot.cluster_high = (self_cluster >> 16) & 0xFFFF;

    //  ".."
    memset(dotdot.name, ' ', 8);
    dotdot.name[0] = '.';
    dotdot.name[1] = '.';
    memset(dotdot.ext, ' ', 3);
    dotdot.attributes = 0x10;
    if (parent_cluster == 0)
    {
        dotdot.cluster_low = self_cluster & 0xFFFF;     // root point to itself
        dotdot.cluster_high = (self_cluster >> 16) & 0xFFFF;
    }
    else
    {
        dotdot.cluster_low = parent_cluster & 0xFFFF;
        dotdot.cluster_high = (parent_cluster >> 16) & 0xFFFF;
    }
    memcpy(&cluster_data[0], &dot, sizeof(dot));
    memcpy(&cluster_data[32], &dotdot, sizeof(dotdot));
}
struct ParsedEntry
{
    char name[256];           // LFN or SFN
    unsigned int first_cluster;
    unsigned char is_directory;
};

int parse_directory(uint8_t *cluster, struct ParsedEntry *entries, int cluster_size, int max_entries)
{
    int count = 0;
    wchar_t lfn_buffer[260];
    int lfn_index = 0;
    memset(lfn_buffer, 0, sizeof(lfn_buffer));

    for (int offset = 0; offset < cluster_size; offset += 32) {
        uint8_t *entry = &cluster[offset];

        // Empty (0x00)
        if (entry[0] == 0x00) break;

        // Deleted (0xE5)
        //if (entry[0] == 0xE5) continue;

        uint8_t attr = entry[11];
        if (attr == 0x0F)
        {
            // LFN
            struct LFNentry *lfn = (struct LFNentry *)entry;
            int seq = lfn->sequence_number & 0x1F; // without LAST_LONG_ENTRY
            int offset_in_lfn = (seq - 1) * 13;

            for (int i = 0; i < 5; i++)
                lfn_buffer[offset_in_lfn + i] = lfn->name1[i];
            for (int i = 0; i < 6; i++)
                lfn_buffer[offset_in_lfn + 5 + i] = lfn->name2[i];
            for (int i = 0; i < 2; i++)
                lfn_buffer[offset_in_lfn + 11 + i] = lfn->name3[i];

            lfn_index = offset_in_lfn + 13;
        }
        else
        {
            // SFN
            struct SFNentry *sfn = (struct SFNentry *)entry;

            if (count >= max_entries) break;

            struct ParsedEntry *pe = &entries[count];
            memset(pe, 0, sizeof(*pe));

            // UTF-16 to UTF-8 (if LFN)
            if (lfn_index > 0)
            {
                wcstombs(pe->name, lfn_buffer, sizeof(pe->name) - 1);
                memset(lfn_buffer, 0, sizeof(lfn_buffer));
                lfn_index = 0;
            }
            else
            {
                // SFN
                char name[9] = {0}, ext[4] = {0};
                memcpy(name, sfn->name, 8);
                memcpy(ext, sfn->ext, 3);

                // Видалити пробіли
                for (int i = 7; i >= 0 && name[i] == ' '; i--) name[i] = '\0';
                for (int i = 2; i >= 0 && ext[i] == ' '; i--) ext[i] = '\0';

                if (ext[0])
                    snprintf(pe->name, sizeof(pe->name), "%s.%s", name, ext);
                else
                    snprintf(pe->name, sizeof(pe->name), "%s", name);
            }

            pe->first_cluster = ((uint32_t)sfn->cluster_high << 16) | sfn->cluster_low;
            pe->is_directory = (sfn->attributes & 0x10) ? 1 : 0;

            count++;
        }
    }

    return count;
}

void create_file_entry(char *name, uint8_t *cluster, unsigned int start_cluster, unsigned int size_bytes, unsigned int cluster_size)
{
    struct SFNentry file = {0};

    memset(file.name, ' ', 8);
    memset(file.ext, ' ', 3);

    char *dot = strchr(name, '.');
    if (dot) {
        strncpy((char*)file.name, name, dot - name);
        strncpy((char*)file.ext, dot + 1, 3);
    } else {
        strncpy((char*)file.name, name, 8);
    }

    file.attributes = 0x20; // archive (файл)
    file.cluster_high = (start_cluster >> 16) & 0xFFFF;
    file.cluster_low = start_cluster & 0xFFFF;
    file.size = size_bytes;

    int offset = find_free_entry_offset(cluster, cluster_size);
    if (offset >= 0) {
        memcpy(&cluster[offset], &file, sizeof(file));
    }
}


int find_free_cluster(uint32_t *FAT, int total_clusters)
{
    for (int i = 2; i <= total_clusters; i++)
    {
        if (FAT[i] == 0) return i;
    }
    return -1; // no free clusters
}

void trim_to_parent(char *path)
{
    int len = strlen(path);
    for (int i = len - 2; i >= 0; i--)
    {
        if (i == 0)
        {
            path[1] = '\0';
            break;
        }
        if (path[i] == '/')
        {
            path[i] = '\0';
            break;
        }
    }
}


long get_file_size(FILE *fp)
{
    long current = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, current, SEEK_SET);
    return size;
}

uint32_t FAT[MAX_FAT_SIZE_BYTES / sizeof(uint32_t)];


int main(int argc, char *argv[])
{

    if(argc < 2 || argc > 2)
    {
        printf("Usage: %s <filedisk_FAT32>\n", argv[0]);
        return 1;
    }

    struct FAT32_BPB bpb;

    FILE *file = fopen(argv[1], "rb+");

    if(file == NULL)
    {
        printf("Okay, creating file (20 MB size). \n");
        file = fopen(argv[1], "rb+");
        fseek(file, 2 * 1024 * 1024 - 1, SEEK_SET); // 2MB - 1
        fputc(0, file); // write byte in order to increase the file size
    }

    long size_file = get_file_size(file);
    fread(&bpb, sizeof(struct FAT32_BPB), 1, file);

    int is_not_fat32 = 0;

    unsigned int current_cluster = 2; // '/' root

    if (bpb.root_cluster == 0)
    {
        is_not_fat32 = 1;
    }

    unsigned int cluster_size = bpb.sectors_per_cluster * bpb.bytes_per_sector;


    uint32_t fat_size_bytes = bpb.fat32_size * bpb.bytes_per_sector;

    fseek(file, bpb.reserved_sectors * bpb.bytes_per_sector, SEEK_SET);

    // read FAT
    fread(FAT, fat_size_bytes, 1, file);
    char user_input[1024];

    char path[1024] = "/";
    char current_folder_name[32] = "/";

    // running the emulator
    while(1)
    {
        printf("%s> ", path);
        fgets(user_input, sizeof(user_input), stdin);
        if(strncmp(user_input, "exit", 4) == 0)
        {
            break;
        }

        if(strncmp(user_input, "ls", 2) == 0)
        {
            if(is_not_fat32)
            {
                printf("Unknown disk format\n");
                continue;
            }

            uint8_t cluster[cluster_size];
            uint32_t first_data_sector = bpb.reserved_sectors + (bpb.fat_amount * bpb.fat32_size);

            uint32_t root_sector = first_data_sector;

            unsigned int offset_current_cluster = (root_sector * bpb.bytes_per_sector) + (current_cluster - 2) * cluster_size;
            fseek(file,  offset_current_cluster, SEEK_SET);

            fread(cluster, cluster_size, 1, file);
            struct ParsedEntry entries[128];


            int count = parse_directory(cluster, entries, cluster_size, 128);


            for(int i = 0; i < count; i++)
            {
                printf("%s ", entries[i].name);
            }
            printf("\n");
        }

        else if(strncmp(user_input, "mkdir ", 6) == 0)
        {
            if(is_not_fat32)
            {
                printf("Unknown disk format\n");
                continue;
            }

            char folder_name[1024];
            sscanf(user_input, "mkdir %s", folder_name);

            strtok(folder_name, " ");
            strtok(folder_name, "\n");
            folder_name[strlen(folder_name)] = '\0';

            // 1. Find free cluster (yeah, we could write this information to FSinfo partion)
            unsigned int data_sectors = bpb.total_sectors - (bpb.reserved_sectors + bpb.fat_amount * bpb.fat32_size);

            unsigned int total_clusters = data_sectors / bpb.sectors_per_cluster;
            unsigned int free_cluster =  find_free_cluster(FAT, total_clusters);
            if (free_cluster == -1)
            {
                printf("No free clusters\n");
                continue;
            }

            FAT[free_cluster] = 0x0FFFFFFF; // mark as used (EOF)

            // 2. ccurrent_cluster
            uint8_t *root_cluster = calloc(1, cluster_size);
            uint32_t first_data_sector = bpb.reserved_sectors + (bpb.fat_amount * bpb.fat32_size);
            uint32_t current_sector = first_data_sector;
            unsigned int offset_current_cluster = (current_sector * bpb.bytes_per_sector) + (current_cluster - 2) * cluster_size;

            fseek(file,  offset_current_cluster, SEEK_SET);
            fread(root_cluster, 1, cluster_size, file);

            // 3. Create folder
            create_folder(folder_name, root_cluster, free_cluster, bpb.bytes_per_sector * bpb.sectors_per_cluster);

            // 4. AND update the current cluster
            fseek(file,  offset_current_cluster, SEEK_SET);
            fwrite(root_cluster, cluster_size, 1, file);
            fflush(file);


            // 5. init '.' and '..'
            uint8_t *new_folder_cluster = calloc(1, cluster_size);
            init_root_directory(new_folder_cluster, free_cluster, bpb.root_cluster); // треба приймати і parent

            int offset_next_cluster = (first_data_sector + (free_cluster - 2) * bpb.sectors_per_cluster) * bpb.bytes_per_sector;

            fseek(file, offset_next_cluster, SEEK_SET);
            fwrite(new_folder_cluster, cluster_size, 1, file);
            fflush(file);

            // 6. writing into FAT
            fseek(file, bpb.reserved_sectors * bpb.bytes_per_sector, SEEK_SET);
            fwrite(FAT, bpb.fat32_size * bpb.bytes_per_sector, 1, file);
            fflush(file);

            free(root_cluster);
            free(new_folder_cluster);
            printf("Created folder: %s (cluster %d)\n", folder_name, free_cluster);
        }
        else if(strncmp(user_input, "format", 6) == 0)
        {
            printf("format\n");

            to_format(&bpb, size_file);

            // Write FAT32 BPB
            fseek(file, 0, SEEK_SET);
            fwrite(&bpb, sizeof(struct FAT32_BPB), 1, file);
            fflush(file);

            // Write FATable
            fat_size_bytes = bpb.fat32_size * bpb.bytes_per_sector;
            memset(FAT, 0, fat_size_bytes);

            FAT[0] = 0x0FFFFFF8; // FATid
            FAT[1] = 0xFFFFFFFF; // reserved
            FAT[2] = 0x0FFFFFFF; // rootdirectory — EOF

            fseek(file, bpb.reserved_sectors * bpb.bytes_per_sector, SEEK_SET);
            fwrite(FAT, fat_size_bytes, 1, file);
            fflush(file);

            if (bpb.fat_amount == 2)
            {
                fwrite(FAT, fat_size_bytes, 1, file); // Копія FAT
            }

            // Miss UEinfo sector
            uint32_t first_data_sector = bpb.reserved_sectors + (bpb.fat_amount * bpb.fat32_size);

            cluster_size = bpb.bytes_per_sector * bpb.sectors_per_cluster;

            uint8_t *cluster = calloc(1, cluster_size);
            init_root_directory(cluster, 2, 0);

            uint32_t root_sector = first_data_sector;
            int test = root_sector * bpb.bytes_per_sector;

            fseek(file, root_sector * bpb.bytes_per_sector, SEEK_SET);
            fwrite(cluster, cluster_size, 1, file);
            fflush(file);

            is_not_fat32 = 0;
            fseek(file, 0, SEEK_SET);


            current_cluster = bpb.root_cluster;

        }
        else if (strncmp(user_input, "cd ", 3) == 0)
        {
            if(is_not_fat32)
            {
                printf("Unknown disk format\n");
                continue;
            }

            char folder_name[32];
            sscanf(user_input, "cd %s", folder_name);

            if (strcmp(folder_name, "/") == 0)
            {
                current_cluster = bpb.root_cluster;
                continue;
            }


            if (strcmp(folder_name, "..") == 0)
            {

                uint8_t *cluster = calloc(1, cluster_size);
                uint32_t first_data_sector = bpb.reserved_sectors + (bpb.fat_amount * bpb.fat32_size);
                uint32_t sector = first_data_sector + (current_cluster - 2) * bpb.sectors_per_cluster;
                uint32_t current_sector = first_data_sector;
                unsigned int offset_current_cluster = (current_sector * bpb.bytes_per_sector) + (current_cluster - 2) * cluster_size;

                fseek(file, offset_current_cluster, SEEK_SET);
                fread(cluster, 1, cluster_size, file);

                struct SFNentry *dotdot = (struct SFNentry *)&cluster[32]; // ".." — другий запис
                uint32_t parent_cluster = ((uint32_t)dotdot->cluster_high << 16) | dotdot->cluster_low;

                if (current_cluster > 2)
                    current_cluster -= 1;

                trim_to_parent(path);
                free(cluster);
                continue;
            }

            // cd <name>
            uint8_t *cluster = calloc(1, cluster_size);
            uint32_t first_data_sector = bpb.reserved_sectors + (bpb.fat_amount * bpb.fat32_size);
            uint32_t sector = first_data_sector + (current_cluster - 2) * bpb.sectors_per_cluster;

            fseek(file, sector * bpb.bytes_per_sector, SEEK_SET);
            fread(cluster, 1, cluster_size, file);

            struct ParsedEntry entries[128];
            int count = parse_directory(cluster, entries, cluster_size, 128);

            int found = 0;
            for (int i = 0; i < count; i++)
            {
                if (strcmp(entries[i].name, folder_name) == 0 && entries[i].is_directory)
                {
                    current_cluster = entries[i].first_cluster;
                    entries[i].name[strlen(entries[i].name)] = '/';
                    strcat(path, entries[i].name);
                    strcpy(current_folder_name, entries[i].name);
                    found = 1;
                    break;
                }
            }

            if (!found)
            {
                printf("Directory not found: %s\n", folder_name);
            }

            free(cluster);

        }
        else if(strncmp(user_input, "touch ", 6) == 0)
        {
            if(is_not_fat32)
            {
                printf("Unknown disk format\n");
                continue;
            }

                char *file_name = user_input + 6;
                file_name[strcspn(file_name, "\n")] = '\0';

                if (strlen(file_name) == 0)
                {
                    printf("Use: touch <filename>\n");
                    continue;
                }


                unsigned int data_sectors = bpb.total_sectors - (bpb.reserved_sectors + bpb.fat_amount * bpb.fat32_size);
                uint32_t first_data_sector = bpb.reserved_sectors + (bpb.fat_amount * bpb.fat32_size);
                uint32_t sector = first_data_sector + (current_cluster - 2) * bpb.sectors_per_cluster;
                unsigned int total_clusters = data_sectors / bpb.sectors_per_cluster;

                // 1.Search for free clusters
                unsigned int free1 = find_free_cluster(FAT, total_clusters);
                if (free1 == -1) { printf("No free cluster\n"); continue; }

                unsigned int free2 = find_free_cluster(FAT, free1);
                if (free2 == -1) { printf("No second free cluster\n"); continue; }

                FAT[free1] = free2;
                FAT[free2] = 0x0FFFFFFF;

                uint8_t *dir_cluster = calloc(1, cluster_size);

                uint32_t current_sector = first_data_sector;
                unsigned int offset_current_cluster = (current_sector * bpb.bytes_per_sector) + (current_cluster - 2) * cluster_size;

                fseek(file, offset_current_cluster, SEEK_SET);
                fread(dir_cluster, 1, cluster_size, file);

                //  SFN in current directory
                unsigned int file_entry_size = cluster_size * 2;
                create_file_entry(file_name, dir_cluster, free1, file_entry_size, cluster_size); // 2 кластери * 4КБ

                // 4. Записати директорію назад
                fseek(file, offset_current_cluster, SEEK_SET);
                fwrite(dir_cluster, cluster_size, 1, file);
                fflush(file);

                // 5. 2 clustres for file data
                uint8_t *file_data = calloc(1, cluster_size);
                uint32_t data_start = first_data_sector * bpb.bytes_per_sector;

                fseek(file, data_start + (free1 - 2) * cluster_size, SEEK_SET);
                fwrite(file_data, cluster_size, 1, file);

                fseek(file, data_start + (free2 - 2) * cluster_size, SEEK_SET);
                fwrite(file_data, cluster_size, 1, file);
                fflush(file);

                // new info in FAT
                fseek(file, bpb.reserved_sectors * bpb.bytes_per_sector, SEEK_SET);
                fwrite(FAT, bpb.fat32_size * bpb.bytes_per_sector, 1, file);
                fflush(file);

                printf("Created file \"%s\" using clusters %d and %d\n", file_name, free1, free2);
                free(file_data);
                free(dir_cluster);

        }
    }

    fclose(file);

    return 0;

}
