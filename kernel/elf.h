// Format of an ELF executable file

#define ELF_MAGIC 0x464C457FU  // "\x7FELF" in little endian

// File header
struct elfhdr {
  uint magic;  // must equal ELF_MAGIC
  uchar elf[12]; // 12字节扩展表示，包含ELF类别(32位/64位)，数据存储方式(大小端)等
  ushort type; // ELF文件类型(可重定位文件，可执行文件，共享库)
  ushort machine; // 目标硬件架构(arm,x86)
  uint version; // ELF版本，通常为1
  uint64 entry; // 程序入口地址(CPU开始执行的虚拟地址)
  uint64 phoff; // 程序头表(program header table)的偏移量(相对于文件的起始位置)
  uint64 shoff; // 节头表(section header table)的偏移量(相对于文件的起始位置)
  uint flags;  // 与目标架构相关的标志位，多数架构下为0
  ushort ehsize; // ELF头部自身的大小（字节）
  ushort phentsize; // 程序头表中每个条目（Program Header）的大小（字节）
  ushort phnum; // 程序头表中条目的数量
  ushort shentsize; // 节头表中每个条目（Section Header）的大小（字节）
  ushort shnum; // 节头表中条目的数量
  ushort shstrndx; 
};

// Program section header
struct proghdr {
  uint32 type; // 段类型：标识这个程序头对应的段用途，比如 1（PT_LOAD，可加载段）、2（PT_DYNAMIC，动态链接信息）、3（PT_INTERP，解释器路径）等
  uint32 flags; // 段的权限标志：比如 0x1（PF_X，可执行）、0x2（PF_W，可写）、0x4（PF_R，可读），组合表示内存段的读写执行权限
  uint64 off; // 该段在 ELF 文件中的偏移量（字节）：表示从文件开头到这个段数据的起始位置
  uint64 vaddr; // 该段加载到内存后的虚拟地址：操作系统会把文件中该段的数据映射到这个虚拟地址上
  uint64 paddr; // 物理地址（仅嵌入式 / 裸机场景有用）：在普通操作系统（如 Linux）中通常和 vaddr 相同，或被忽略
  uint64 filesz; // 该段在文件中的大小（字节）：比如代码段在文件中是压缩 / 对齐后的大小
  uint64 memsz; // 该段加载到内存中的大小（字节）：通常 ≥ filesz，差值部分会被初始化为 0（比如 BSS 段，文件中无数据但内存中需分配空间）
  uint64 align; // 段的对齐要求：加载时虚拟地址和文件偏移都要按这个值对齐（通常是页大小，如 4096 字节）
};

// Values for Proghdr type
#define ELF_PROG_LOAD           1

// Flag bits for Proghdr flags
#define ELF_PROG_FLAG_EXEC      1
#define ELF_PROG_FLAG_WRITE     2
#define ELF_PROG_FLAG_READ      4
