/**
* author: liulei2010@ict.ac.cn
* @20130629
* sequentially scan the page table to check and re-new __access_bit, and cal. the number of hot pages. 
* add shadow count index
*
* Modifications@20150130 recover from an unknown problem. This version works well.
* By Lei Liu, Hao Yang, Mengyao Xie, Yong Li, Mingjie Xing
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/kernel.h>
#include <linux/sem.h>
#include <linux/list.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/hugetlb.h>
#include <linux/mman.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/ksm.h>
#include <linux/rmap.h>
#include <linux/module.h>
#include <linux/delayacct.h>
#include <linux/init.h>
#include <linux/writeback.h>
#include <linux/memcontrol.h>
#include <linux/mmu_notifier.h>
#include <linux/kallsyms.h>
#include <linux/swapops.h>
#include <linux/elf.h>
#include <linux/sched.h>

/* Adjust these constants as required */
#define ITERATIONS 200 /* the number of sampling loops */
#define PAGE_ALL 300000 /* the total number of pages */
#define TIME_INTERVAL 5 /* the total number of pages */
/* the constants below denote the different ranges of the page number */
#define VH 200 /* VH means very high*/
#define H 150 /* H means high*/
#define M 100 /* M means middle*/
#define L 64 /* L means low */
#define VL_MAX 10 /* VL means very low */
#define VL_MIN 5

static int scan_pgtable(void);
static struct task_struct * traverse_all_process(void);

struct timer_list stimer; 
/*the handle of current running benchmark*/
struct task_struct * bench_process;
/* the array that records page access freq., which reflects the page "heat"*/
long long page_heat[PAGE_ALL];
/* pid passed in.Write by xiemengyao.*/
static int process_id;
module_param(process_id, int, S_IRUGO|S_IWUSR);

/**
* begin to cal. the number of hot pages.
* And we will re-do it in every TIME_INTERVAL seconds.
*/
static void time_handler(unsigned long data)
{ 
	int win=0;
     	mod_timer(&stimer, jiffies + TIME_INTERVAL*HZ);
     	win = scan_pgtable(); /* 1 is win.*/
     	if(!win) /* we get no page, maybe something wrong occurs.*/
        	printk("sysmon: fail in scanning page table...\n");
}

static int __init timer_init(void)
{
	printk("sysmon: module init!\n");
     	init_timer(&stimer);
     	stimer.data = 0;
     	stimer.expires = jiffies + TIME_INTERVAL*HZ;
     	stimer.function = time_handler;
     	add_timer(&stimer);
     	return 0;
}

static void __exit timer_exit(void)
{
	printk("Unloading sysmon module.\n");
     	del_timer(&stimer);/*delete the timer*/
     	return;
}

/**
* Write by xiemengyao.
* get the process of current running benchmark.
* The returned value is the pointer to the process.
*/
static struct task_struct * traverse_all_process(void) 
{
	struct pid * pid;
  	pid = find_vpid(process_id);
  	return pid_task(pid,PIDTYPE_PID);
}

/*pgtable sequential scan and count for __access_bits.*/
static int scan_pgtable(void)
{
	pgd_t *pgd = NULL;
     	pud_t *pud = NULL;
     	pmd_t *pmd = NULL;
     	pte_t *ptep, pte;
     	spinlock_t *ptl;

     	struct mm_struct *mm;
     	struct vm_area_struct *vma;
     	unsigned long start = 0; /*the start of address.*/
     	unsigned long end = 0;   /*the end of address.*/
     	unsigned long address = 0;
     	int number_hotpages = 0; /* the number of hot pages */
     	int number_vpages = 0;
     	int cycle_index = 0; /* the loop counter, which denotes the ITERATIONS. */
     	/* the array that records the number of hot page in every cycle */
     	int hot_page[ITERATIONS];
     	int j = 0;
     	int pg_count = 0; /* The temporary counter*/
     	int number_current_pg = 0;/* The temporary counter*/
     
      	/* some variables that describe page "heat" */
     	int hig = 0;
     	int mid = 0;
     	int low = 0;
     	int llow = 0;
     	int lllow = 0;
     	int llllow = 0;
     	int all_pages = 0;/* the total number of pages */
     	/*the average number of hot pages in each iteration.*/
     	long avg_hotpage=0;
     	/*the total number of memory accesses across all pages*/
     	long num_access=0;
     	/* avg utilization of each page */
     	int avg_page_utilization = 0;
     
     	/*get the handle of current running benchmark*/
     	bench_process = traverse_all_process();

     	if(bench_process == NULL)
     	{
        	printk("sysmon: get no process handle in scan_pgtable function...exit&trying again...\n");
          	return 0;
     	}
     	else /*get the process*/
        	mm = bench_process->mm;
     	if(mm == NULL)
     	{
        	printk("sysmon: error mm is NULL, return back & trying...\n");       
          	return 0;
     	}

     	for(j = 0; j < PAGE_ALL; j++)
        	page_heat[j] = -1;  
     	for(j = 0; j < ITERATIONS; j++)
        	hot_page[j] = 0;
           
     	for(cycle_index = 0; cycle_index < ITERATIONS; cycle_index++)
     	{
       		number_hotpages = 0;
       		/*scan each vma*/
       		for(vma = mm->mmap; vma; vma = vma->vm_next)
       		{
	  		start = vma->vm_start; 
          		end = vma->vm_end;
          		mm = vma->vm_mm;
          		/*in each vma, we check all pages */
          		for(address = start; address < end; address += PAGE_SIZE)
	      		{
	           		/*scan page table for each page in this VMA*/
	           		pgd = pgd_offset(mm, address);
 	           		if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
                   		continue;
	           		pud = pud_offset(pgd, address);
	           		if (pud_none(*pud) || unlikely(pud_bad(*pud)))
                     			continue;
                   		pmd = pmd_offset(pud, address);
	           		if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
		        	continue;
               	   		ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	           		pte = *ptep;
	           		if(pte_present(pte)) 
				{   
                     			if(pte_young(pte)) /* hot page*/ 
					{
		           			/*re-set and clear  _access_bits to 0*/
                           			pte = pte_mkold(pte); 
                           			set_pte_at(mm, address, ptep, pte);
			 		}
		   		}
		     		else /*no page pte_none*/ 
				{
		      			pte_unmap_unlock(ptep, ptl);
		      			continue;
		  		}
               			pte_unmap_unlock(ptep, ptl);  
          		} /*end for(adddress .....)*/
       		} /*end for(vma ....)*/
 
        /*count the number of hot pages*/
	if(bench_process == NULL)
        {   
        	printk("sysmon: get no process handle in scan_pgtable function...exit&trying again...\n");
           	return 0;
        }   
        else /* get the process*/
           	mm = bench_process->mm;
        if(mm == NULL)
        {   
           	printk("sysmon: error mm is NULL, return back & trying...\n");
           	return 0;
        } 
        number_vpages = 0;
        for(vma = mm->mmap; vma; vma = vma->vm_next)
        {
           	start = vma->vm_start;
           	end = vma->vm_end;
           	/*scan each page in this VMA*/
           	mm = vma->vm_mm;
           	pg_count = 0; 
           	for(address = start; address < end; address += PAGE_SIZE)
           	{
               		/*scan page table for each page in this VMA*/
               		pgd = pgd_offset(mm, address);
               		if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
                     		continue;
               		pud = pud_offset(pgd, address);
               		if (pud_none(*pud) || unlikely(pud_bad(*pud)))
                     		continue;
               		pmd = pmd_offset(pud, address);
               		if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
                     		continue;
               		ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
               		pte = *ptep;
               		if(pte_present(pte))
               		{
                     		if(pte_young(pte)) /*hot pages*/
                     		{
                           		number_current_pg = pg_count + number_vpages;
                           		page_heat[number_current_pg]++;
                           		hot_page[cycle_index]++;
                     		}
               		}
               		pg_count++;
               		pte_unmap_unlock(ptep, ptl);
           	} /*end for(address ......)*/
           	number_vpages += (int)(end - start)/PAGE_SIZE;
             } /*end for(vma .....) */ 
	 } /*end ITERATIONS times repeats*/
      
/*****************************OUTPUT************************************/

	for(j = 0; j < PAGE_ALL; j++)
      	{
          	if(page_heat[j] < VH && page_heat[j] > H)
              		hig++;
          	if(page_heat[j] > M && page_heat[j] <= H)
              		mid++;
          	if(page_heat[j] <= M && page_heat[j] > L)
              		low++;
          	if(page_heat[j] > VL_MAX && page_heat[j] <= L)
              		llow++;
          	if(page_heat[j] > VL_MIN && page_heat[j] <= VL_MAX)
              		lllow++;
          	if(page_heat[j] >= 0 && page_heat[j] <= VL_MIN)
              		llllow++;
          	if(page_heat[j] > -1)
              		all_pages++;
      	}

      	/*the values reflect the accessing frequency of each physical page.*/
      	printk("[LOG: after sampling (%d loops) ...] ",ITERATIONS);
      	printk("the values denote the physical page accessing frequence.\n"); 
      	printk("-->hig (150,200) is %d. Indicating the number of re-used pages is high.\n",hig);
      	printk("-->mid (100,150] is %d.\n",mid);
      	printk("-->low (64,100] is %d.\n",low);
      	printk("-->llow (10,64] is %d. In locality,no too many re-used pages.\n",llow);
      	printk("-->lllow (5,10] is %d.\n",lllow);
      	printk("-->llllow [1,5] is %d.\n",llllow);

      	/*the average number of hot pages in each iteration.*/
      	for(j = 0; j < ITERATIONS; j++)
         	avg_hotpage += hot_page[j];
      	avg_hotpage /= (j + 1); 

      	/*  
       	* new step@20140704
       	* (1)the different phases of memory utilization
       	* (2)the avg. page accessing utilization
       	* (3)memory pages layout and spectrum
       	*/

      	/*the total number of memory accesses across all pages*/
      	for(j = 0; j < PAGE_ALL; j++)
          	if(page_heat[j] > -1) /*the page that is accessed at least once*/
              		num_access += (page_heat[j] + 1);

      	printk("the total number of memory accesses is %ld, the average is %ld\n",
		num_access, num_access/ITERATIONS);
      	avg_page_utilization = num_access / all_pages;
      	printk("Avg hot pages num is %ld, all used pages num is %d, avg utilization of each page is %d\n",
		avg_hotpage,all_pages,avg_page_utilization);
      	printk("\n");
      	return 1;
}

module_init(timer_init);
module_exit(timer_exit);
MODULE_AUTHOR("leiliu");
MODULE_LICENSE("GPL");
