#ifndef  _ZJUNIX_PAGH_H
#define  _ZJUNIX_PAGE_H

#define  PAGE_SHIFT   12                    // shift of page
#define  PAGE_SIZE    (1 << PAGE_SHIFT)     // size of page
#define  PAGE_MASK    (~(PAGE_SIZE - 1))    // lowest 10 digits -> 0

#define  INDEX_MASK    0x3ff                // lowest 10 digits -> 1
#define  PGD_SHIFT     22                   // pgd -> highest 10 digits
#define  PGD_SIZE     (1 << PAGE_SHIFT)
#define  PGD_MASK     (~((1 << PGD_SHIFT) - 1))

typedef unsigned int pgd_t;
typedef unsigned int pte_t;

int do_one_mapping(pgd_t *pgd, unsigned int va, unsigned int pa, unsigned int attr);
int do_mapping(pgd_t *pgd, unsigned int va, unsigned int npage, unsigned int pa, unsigned int attr);

#endif