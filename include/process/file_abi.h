#ifndef GTOS_PROCESS_FILE_ABI_H
#define GTOS_PROCESS_FILE_ABI_H
// Additive IA32 native ABI. EAX=operation, EBX=request VA, ECX=exact bytes.
// EAX=signed result; other registers/segments survive. No caller-supplied owner.
#define GTOS_FILE_ABI_VERSION 1U
#define GTOS_SYS_FILE_OPEN 0x4720U
#define GTOS_SYS_FILE_READ 0x4721U
#define GTOS_SYS_FILE_WRITE 0x4722U
#define GTOS_SYS_FILE_SEEK 0x4723U
#define GTOS_SYS_FILE_CLOSE 0x4724U
#define GTOS_SYS_FILE_SYNC 0x4725U
#define GTOS_SYS_FILE_TRUNCATE 0x4726U
#define GTOS_SYS_FILE_STAT 0x4727U
#define GTOS_SYS_FILE_MKDIR 0x4728U
#define GTOS_SYS_FILE_REMOVE 0x4729U
#define GTOS_SYS_FILE_RENAME 0x472aU
#define GTOS_SYS_FILE_DIR_OPEN 0x472bU
#define GTOS_SYS_FILE_DIR_READ 0x472cU
#define GTOS_SYS_FILE_DIR_REWIND 0x472dU
#define GTOS_SYS_FILE_SIZE 0x472eU
#define GTOS_SYS_FILE_HANDLE_INFO 0x472fU
#define GTOS_FILE_PATH_LIMIT 512U
#define GTOS_FILE_TRANSFER_LIMIT 4096U
#define GTOS_FILE_READ 1U
#define GTOS_FILE_WRITE 2U
#define GTOS_FILE_READ_WRITE 3U
#define GTOS_FILE_CREATE 0x100U
#define GTOS_FILE_EXCLUSIVE 0x200U
#define GTOS_FILE_TRUNCATE 0x400U
#define GTOS_FILE_APPEND 0x800U
#define GTOS_FILE_TYPE_REGULAR 1U
#define GTOS_FILE_TYPE_DIRECTORY 2U
#define GTOS_FILE_ERR_BAD_ADDRESS (-14)
#define GTOS_FILE_ERR_TOO_LARGE (-7)
#define GTOS_FILE_ERR_UNAVAILABLE (-19)
#define GTOS_FILE_ERR_INVALID (-22)
#define GTOS_FILE_ERR_VERSION (-38)
#define GTOS_FILE_INFO_BYTES 268U

typedef struct GtosFileOpenRequest { unsigned version,path,path_bytes,flags; } GtosFileOpenRequest;
typedef struct GtosFileTransferRequest { unsigned version,handle,buffer,bytes; } GtosFileTransferRequest;
typedef struct GtosFileSeekRequest { unsigned version,handle,offset_low,offset_high,whence; } GtosFileSeekRequest;
typedef struct GtosFileControlRequest { unsigned version,handle; } GtosFileControlRequest;
typedef struct GtosFileTruncateRequest { unsigned version,handle,bytes; } GtosFileTruncateRequest;
typedef struct GtosFilePathRequest { unsigned version,path,path_bytes; } GtosFilePathRequest;
typedef struct GtosFileStatRequest { unsigned version,path,path_bytes,result,result_bytes; } GtosFileStatRequest;
typedef struct GtosFileRenameRequest { unsigned version,old_path,old_bytes,new_path,new_bytes; } GtosFileRenameRequest;
typedef struct GtosFileInfoRequest { unsigned version,handle,result,result_bytes; } GtosFileInfoRequest;
typedef struct GtosFileInfo { unsigned version,type,bytes;char name[256]; } GtosFileInfo;

// Paths include their final NUL, with no earlier NUL, and at least one byte
// before it. Bytes (including UTF-8) are copied unchanged to the actual store.
// Handle info has an empty name; path stat/directory read return the real name.
// Read/write validate the entire requested user range before disk I/O, including
// short reads and EOF. Zero transfers still require an address in the user arena.
// Descriptors and source/path/output may overlap, after the complete snapshot.
// Validation errors leave file offsets, contents, handles and outputs unchanged;
// actual I/O errors can have partial effects and are propagated, not hidden.
// DIR_READ returns 1 per entry, 0 at EOF; EOF leaves output unchanged.
// Close errors still invalidate the handle. Exit/fault/cancel closes are deferred
// to kernel Reap, which reports aggregate failures through kernel statistics.
// This is a native file service, not a Linux syscall or complete POSIX runtime.
#if defined(__cplusplus)
static_assert(sizeof(unsigned)==4,"File ABI unsigned");
static_assert(sizeof(GtosFileOpenRequest)==16,"File open ABI");
static_assert(sizeof(GtosFileTransferRequest)==16,"File transfer ABI");
static_assert(sizeof(GtosFileSeekRequest)==20,"File seek ABI");
static_assert(sizeof(GtosFileControlRequest)==8,"File control ABI");
static_assert(sizeof(GtosFileTruncateRequest)==12,"File truncate ABI");
static_assert(sizeof(GtosFilePathRequest)==12,"File path ABI");
static_assert(sizeof(GtosFileStatRequest)==20,"File stat ABI");
static_assert(sizeof(GtosFileRenameRequest)==20,"File rename ABI");
static_assert(sizeof(GtosFileInfoRequest)==16,"File info request ABI");
static_assert(sizeof(GtosFileInfo)==GTOS_FILE_INFO_BYTES,"File info ABI");
#endif
#endif
