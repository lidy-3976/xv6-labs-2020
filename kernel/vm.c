#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

/*
 * create a direct-map page table for the kernel.
 */
void
kvminit()
{
  kernel_pagetable = (pagetable_t) kalloc();
  memset(kernel_pagetable, 0, PGSIZE);

  // uart registers
  kvmmap(kernel_pagetable, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kernel_pagetable, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  kvmmap(kernel_pagetable, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(kernel_pagetable, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kernel_pagetable, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kernel_pagetable, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  // 将trampline页面映射到内核虚拟地址空间的最高一个页面
  // TRAMPOLINE的定义如下，就是最高虚拟地址减去一个页面大小
  // #define TRAMPOLINE (MAXVA - PGSIZE)
  // 注意阅读上面的链接脚本时，我们将trampsec段放置在了内核代码后面
  // 那其实也是trampoline的开头，也就是说我们其实映射了trampoline页面两次
  kvmmap(kernel_pagetable, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
}

pagetable_t 
my_kvminit_newpgtbl()
{
  pagetable_t pgtbl = (pagetable_t) kalloc();
  memset(pgtbl,0,PGSIZE);

   // uart registers
  kvmmap(pgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(pgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // CLINT
  // CLINT映射只在内核启动的时候会用到，而用户进程在内核态中的操作并不需要用到该映射
  // 若保留该映射，会与从进程用户页表复制过来的映射冲突
  //kvmmap(pgtbl, CLINT, CLINT, 0x10000, PTE_R | PTE_W);

  // PLIC
  kvmmap(pgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(pgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(pgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  return pgtbl;
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  //设置内核根页表寄存器的，一旦设置完毕之后相当于也打开了分页机制，
  //自此之后虚拟地址就要经过MMU的翻译才可以转化为物理地址了
  //但是在内核态下因为大部分页面执行的还是直接映射，所以物理地址和
  //虚拟地址本质上还是相等的(除了内核栈和trampoline页面)
  w_satp(MAKE_SATP(kernel_pagetable));
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA) //虚拟地址超过最大值，陷入错误
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)]; //pte_t表示页表项，64位整数 取页表数组中对应索引位置的页表项的地址
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte); //如果有效，接着下一层索引
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
// 专门用来查找用户页表中特定虚拟地址va所对应的物理地址
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)//用户无权访问
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
// 在内核页表添加一个映射项，且此函数仅在启动时初始化内核页表时使用，仅仅是mappages函数薄薄的一层封装与调用
void
kvmmap(pagetable_t pagetable,uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(pagetable, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// translate a kernel virtual address to
// a physical address. only needed for
// addresses on the stack.
// assumes va is page aligned.
uint64
kvmpa(pagetable_t pgtbl, uint64 va)
{
  uint64 off = va % PGSIZE;
  pte_t *pte;
  uint64 pa;
  
  pte = walk(pgtbl, va, 0);
  if(pte == 0)
    panic("kvmpa");
  if((*pte & PTE_V) == 0)
    panic("kvmpa");
  pa = PTE2PA(*pte);
  return pa+off;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
// 建立虚拟地址到物理地址映射（连续） perm表示访问权限
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va); //向下取整到页面起始位置
  last = PGROUNDDOWN(va + size - 1);
  //迭代建立映射关系
  for(;;){
    //调用walk函数，返回当前地址a对应的PTE
    //如果返回空指针，说明walk没能有效建立新的页表页，这可能是内存耗尽导致的
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    //如果找到了页表项，但是有效位已经被置位，表示这块物理内存已经被使用
    //这说明原本的虚拟地址va根本不足以支撑分配size这么多的连续空间，陷入panic
    if(*pte & PTE_V)
      panic("remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    //设置完当前页之后看看是否到达设置的最后一页，是则跳出循环
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
// uvmunmap函数和freewalk函数是组合使用的，前面我们在看freewalk函数时
// 还记得它负责释放的是页表页，那么这里的uvmunmap负责的就是释放叶级页表
// 中PTE记录的映射关系，特别地，如果设置标志位do_free，此函数还会一并将分配出去的物理页面也进行回收
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  //va不是页对齐的，陷入panic
  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    //查找成功，但发现此PTE不存在，陷入panic
    if((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    //查找成功，但发现此PTE除了valid位有效外，其他位均为0
    //这暗示这个PTE原本不应该出现在叶级页表(奇怪的错误)，陷入panic
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    //否则这是一个合法的，应该被释放的PTE
    //如果do_free被置位，那么还要释放掉PTE对应的物理内存
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    //最后将PTE本身全部清空，成功解除了映射关系
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  //mem虽然是一个指针，但是因为内核地址空间中虚拟地址和物理地址
  //在RAM上是直接映射的，所以它其实也就等于物理地址
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  //分配一页物理内存作为initcode的存放处，memset用来将当前页清空
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  //在页表中加入一条虚拟地址0 <-> mem的映射，相当于将initcode成功映射到了虚拟地址0
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  //将initcode的代码一个字节一个字节地搬运到mem地址
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
// 为用户进程向内核申请更多的内存
// PGROUNDUP(sz)：sz大小的内存至少使用多少页才可以存下,返回的是下一个未使用页的地址
// PGROUNDUP(sz)的使用对象是内存大小
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  //如果新的内存大小更小，不用分配，直接返回旧内存大小
  if(newsz < oldsz)
    return oldsz;
  //崭新的一页
  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    //如果分配成功，则将新分配的页面全部清空
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
// 回收用户页表中的页面，将用户进程中已经分配的空间大小从oldsz修改到newsz，并返回新地址空间的大小
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  //如果新的内存大小比原先内存还要大，那么什么也不用做，直接返回oldsz即可
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    //计算出来要释放的页面数量
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    //调用uvmunmap，清空叶级页表的PTE并释放物理内存
    //因为我们使用了PGROUNDUP来取整页面数量，所以这里可以保证va是页对齐的
    //因为用户地址空间是从地址0开始紧密排布的， 所以PGROUNDUP(newsz)对应着新内存大小的结束位置
    //注意do_free置为1，表示一并回收物理内存
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
// 回收页表页内存
// 在调用这个函数时应该保证叶子级别页表的映射关系全部解除并释放
// (这将会由后面的uvmunmap函数负责)，因为此函数专门用来回收页表页
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    //如果有效位为1，且读位、写位、可执行位都是0
    //说明这是一个高级别(非叶级)页表项，且此项未被释放，应该去递归地释放
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
    //如果有效位为1，且读位、写位、可执行位有一位为1
    //表示这是一个叶级PTE，且未经释放，这不符合本函数调用条件，会陷入一个panic
  }
  kfree((void*)pagetable);
}

//释放内核页表的所有映射但不释放其指向的物理页
void
my_kvm_free_kernelpgtbl(pagetable_t pgtbl)
{
  for(int i=0;i<512;i++){
    pte_t pte=pgtbl[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X))==0){
      uint64 child = PTE2PA(pte);
      my_kvm_free_kernelpgtbl((pagetable_t)child);
    }
    pgtbl[i] = 0; 
  }
  kfree((void*)pgtbl);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
// uvmcopy函数是为fork系统调用服务的，它会将父进程
// 的整个地址空间全部复制到子进程中，这包括页表本身和页表指向的物理内存中的数据。
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
// 为什么内核目的地址用指针来表示，而用户态的地址却用unint64来表示？
// 由于copyin运行在内核态下，所以在copyin代码中凡是引用指针变量的地方
// (如dst)都会通过MMU硬件单元查询内核页表翻译为对应的物理地址,而对于
// 用户态下的虚拟地址，我们就没法使用MMU来翻译了，因为在内核态下地址空间
// 是内核地址空间而非用户地址空间。我们只能够用软件来模拟MMU的查找过程，
// 这也就是在copyin代码中调用walkaddr的原因，它本质上是用软件逻辑实现了硬件MMU的地址翻译过程。
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  return copyin_new(pagetable, dst, srcva, len);
  /*
  uint64 n, va0, pa0;

  //总共要复制len个字符
  while(len > 0){
    //找对应页的起始虚拟地址
    va0 = PGROUNDDOWN(srcva);
    //找到va0对应的实际物理地址
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
  */
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  return copyinstr_new(pagetable, dst, srcva, max);
  /*
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
  */
}

//拷贝页表项从用户页表到进程独享的内核页表,不拷贝实际的物理内存
int
my_copymappings(pagetable_t src, pagetable_t dst, uint64 start, uint64 sz)
{
  pte_t* pte;
  uint64 i,pa;
  //start向上取整，防止重复映射已经映射的页
  for(i = PGROUNDUP(start); i < start + sz; i += PGSIZE){
    if((pte = walk(src, i, 0)) == 0)
      panic("my_copymappings: cannot walk to pte");
    if((*pte & PTE_V) == 0)
      panic("my_copymappings: pte is invalid");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("my_copymappings: not a leaf");
    
    pa = PTE2PA(*pte);
    int perm = PTE_FLAGS(*pte) & ~PTE_U;
    if(mappages(dst, i, PGSIZE, pa, perm) != 0){
      goto err;
    }
  }
  return 0;

err:
  //解除目标页表已映射的页表项
  uvmunmap(dst, PGROUNDUP(start), (i - PGROUNDUP(start)) / PGSIZE, 0);
  return -1;
}

//与uvmdealloc功能类似，将程序内存从oldsz缩减到newsz, 只是不释放物理内存
uint64 
my_kvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    //计算出来要释放的页面数量
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    //调用uvmunmap，清空叶级页表的PTE并释放物理内存
    //因为我们使用了PGROUNDUP来取整页面数量，所以这里可以保证va是页对齐的
    //因为用户地址空间是从地址0开始紧密排布的， 所以PGROUNDUP(newsz)对应着新内存大小的结束位置
    //注意do_free置为0，表示不回收物理内存
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 0);
  }
  return newsz;
}

//模拟查询页表的过程，对三级页表进行遍历打印
int print_pagetable(pagetable_t pagetable,int depth)
{
  for(int i=0;i<512;i++){
    pte_t pte=pagetable[i];

    if(pte & PTE_V){
      for(int j=0;j<depth;j++){
        printf("..");
      }
      printf("%d: pte %p pa %p\n",i,pte,PTE2PA(pte));

      if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
        uint64 child=PTE2PA(pte);
        print_pagetable((pagetable_t)child,depth+1);
      }
    }
  }
  return 0;
}

int vm_print(pagetable_t pagetable)
{
  printf("page table %p\n",pagetable);
  return print_pagetable(pagetable,1);
}