// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int err_no;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if (!(err & FEC_PR) || !(uvpt[PGNUM(addr)] & PTE_COW)){
		panic("[pgfault] fault on no COW page %x", addr);
	}

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	err_no = sys_page_alloc(0, (void *)PFTEMP, PTE_P | PTE_U | PTE_W);
	if (err_no < 0) {
		panic("[pgfault] [sys_page_alloc] failed: %e", err_no);
	}
	uintptr_t fault_pg = ROUNDDOWN((uintptr_t)addr, PGSIZE);

	memcpy((void *)PFTEMP, (void *)fault_pg, PGSIZE);

	err_no = sys_page_map(0, PFTEMP, 0, (void *)fault_pg, PTE_P | PTE_U | PTE_W);
	if (err_no < 0) {
		panic("[pgfault] [sys_page_map] failed: %e", err_no);
	}
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int err;

	// LAB 4: Your code here.
	int perm = PTE_P | PTE_U;

	// If the page is writable or copy-on-write
	if (uvpt[pn] & (PTE_W | PTE_COW)) {
		int new_perm = perm | PTE_COW;

		err = sys_page_map(0, (void *)(pn * PGSIZE), envid, (void *)(pn * PGSIZE), new_perm);
		if (err < 0) {
			panic("[duppage] failed to map parent(W|COW) -> child: %e", err);
		}
		err = sys_page_map(0, (void *)(pn * PGSIZE), 0, (void *)(pn * PGSIZE), new_perm);
		if (err < 0) {
			panic("[duppage] failed to remap -> parent(COW): %e", err);
		}
	} else {
		err = sys_page_map(0, (void *)(pn * PGSIZE), envid, (void *)(pn * PGSIZE), perm);
		if (err < 0) {
			panic("[duppage] failed to map parent(P|U) -> child: %e\n", err);
		}
	}
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
		// Set the page fault handler
	set_pgfault_handler(pgfault);

	envid_t envid;
	int err;

	// Allocate a new child environment.
	// The kernel will initialize it with a copy of our register state,
	// so that the child will appear to have called sys_exofork() too -
	// except that in the child, this "fake" call to sys_exofork()
	// will return 0 instead of the envid of the child.
	envid = sys_exofork();
	if (envid < 0)
		panic("sys_exofork: %e", envid);
	if (envid == 0) {
		// We're the child.
		// The copied value of the global variable 'thisenv'
		// is no longer valid (it refers to the parent!).
		// Fix it and return 0.
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	// We're the parent.
	// Copy our mappings into the child.
	int perm = (PTE_P | PTE_U);
	for (uintptr_t addr = 0; addr < USTACKTOP; addr += PGSIZE) {
		bool pt_valid = ((uvpd[PDX(addr)] & perm) == perm);
		if (!pt_valid)
			continue;

		bool pg_valid = ((uvpt[PGNUM(addr)] & perm) == perm);
		if (!pg_valid)
			continue;

		duppage(envid, PGNUM(addr));
	}

	// Allocate the exception stack for the child environment
	err = sys_page_alloc(envid, (void *)(UXSTACKTOP - PGSIZE), PTE_P | PTE_U | PTE_W);
	if (err < 0) {
		panic("[fork] [sys_page_alloc]: %e", err);
	}

	// Install the page fault upcall fro the child environment
	extern void _pgfault_upcall();
	err = sys_env_set_pgfault_upcall(envid, _pgfault_upcall);
	if (err < 0) {
		panic("[fork] [sys_env_set_pgfault_upcall]: %e", err);
	}

	// Start the child environment running
	err = sys_env_set_status(envid, ENV_RUNNABLE);
	if (err < 0) {
		panic("[fork] [sys_env_set_status]: %e", err);
	}

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
