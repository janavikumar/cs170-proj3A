/*
 * VirtualMemoryManager implementation
 *
 * Used to facilitate demand paging through providing a means by which page
 * faults can be handled and pages loaded from and stored to disk.
*/

#include <stdlib.h>
#include <machine.h>
#include "virtualmemorymanager.h"
#include "system.h"

VirtualMemoryManager::VirtualMemoryManager()
{
    fileSystem->Create(SWAP_FILENAME, SWAP_SECTOR_SIZE * SWAP_SECTORS);
    swapFile = fileSystem->Open(SWAP_FILENAME);

    swapSectorMap = new BitMap(SWAP_SECTORS);
    physicalMemoryInfo = new FrameInfo[NumPhysPages];
    for(int i = 0; i < NumPhysPages; i++){
        //go through the physical memory and set each AddrSpace page to null
        physicalMemoryInfo[i].space = NULL;
    }

    //swapSpaceInfo = new SwapSectorInfo[SWAP_SECTORS];

    nextVictim = 0;
}

VirtualMemoryManager::~VirtualMemoryManager()
{
    fileSystem->Remove(SWAP_FILENAME);
    delete swapFile;
    delete [] physicalMemoryInfo;
    //delete [] swapSpaceInfo;
}

int VirtualMemoryManager::allocSwapSector()
{
    int location = swapSectorMap->Find() * PageSize; // also marks the bit
    return location;
}
/*
SwapSectorInfo * VirtualMemoryManager::getSwapSectorInfo(int index)
{
    return swapSpaceInfo + index;
    
}
*/
void VirtualMemoryManager::writeToSwap(char *page, int pageSize,
                                       int backStoreLoc)
{
    swapFile->WriteAt(page, pageSize, backStoreLoc);
}

/*
 * Page replacement with  the second chance algorithm
 */
void VirtualMemoryManager::swapPageIn(int virtAddr)
{

        TranslationEntry* currPageEntry;
        
        //create a stack frame that holds information about the pages in the memory manager
        FrameInfo* physicalPageInfo = nextVictim + physicalMemoryInfo;
        //create an object to hold the translation between the old physical page and the its virtual counterpart
        TranslationEntry* prevPageEntry;

        //set up a sort of circular queue; if all the pages have their
        //referenced bit set, on the second encounter of the first page
        //in the list, the page will be swapped out, as it will
        //have its reference bit cleared. if all pages have it cleared
        //implement FIFO scheme; if no more space, return

        if(nextVictim>= NumPhysPages) {//no more space available
                fprintf(stderr, "Fatal error: No more space available\n");
                exit(1);
                return;
        }

        //if there is space, find the next open place from front of the queue
        while(physicalPageInfo->space != NULL && getPageTableEntry(physicalPageInfo)->use == true){
            //if the reference bit is set for current page in stack then clear it
                getPageTableEntry(physicalPageInfo)->use = false;
                //increment the spot you're looking at
                nextVictim++;
                nextVictim %= NumPhysPages;
                //in the stack, move to the next location (remember to % NumPhysPages
                //as this works in a cyclical queue and needs to wrap around
                physicalPageInfo = physicalMemoryInfo + (nextVictim);
            
        }

        //now, if there is space in the stack frame for physical pages
	//if there is no space, however, make room then increment nextVictim
	//to move to the correct point in the stack for the following slot search
        //find the victim page and swap
        if(physicalPageInfo->space == NULL){
            //point the stack to a new physical page structure
            *physicalPageInfo = FrameInfo();
            int newIndex = virtAddr/PageSize;
            physicalPageInfo->pageTableIndex = newIndex;
            physicalPageInfo->space = currentThread->space;
            currPageEntry = getPageTableEntry(physicalPageInfo);
            //access the page number in real memory and set 
            currPageEntry->physicalPage = memoryManager->getPage();
            loadPageToCurrVictim(virtAddr);

	}


        else{
	    //set the old page
            prevPageEntry = getPageTableEntry(physicalPageInfo);
            bool d = prevPageEntry->dirty;
            if(d == true){
                char* mmMemory = machine->mainMemory + prevPageEntry->physicalPage * PageSize;
                int index = physicalPageInfo->pageTableIndex;
                TranslationEntry* sw = physicalPageInfo->space->getPageTableEntry(index);
                writeToSwap(mmMemory, PageSize, sw->locationOnDisk);
                prevPageEntry->dirty = false;
            }

            //swap the old page with the current page
            physicalPageInfo->space = currentThread->space;
            int newIndex = virtAddr/PageSize;
            physicalPageInfo->pageTableIndex = newIndex;
            currPageEntry = getPageTableEntry(physicalPageInfo);
            currPageEntry->physicalPage = prevPageEntry->physicalPage;
            loadPageToCurrVictim(virtAddr);
            prevPageEntry->valid = false;

        }
    
        nextVictim++;
        nextVictim = nextVictim % NumPhysPages;

        
}


/*
 * Cleanup the physical memory allocated to a given address space after its 
 * destructor invokes.
*/
void VirtualMemoryManager::releasePages(AddrSpace* space)
{
    for (int i = 0; i < space->getNumPages(); i++)
    {
        TranslationEntry* currPage = space->getPageTableEntry(i);
    //  int swapSpaceIndex = (currPage->locationOnDisk) / PageSize;
 //     SwapSectorInfo * swapPageInfo = swapSpaceInfo + swapSpaceIndex;
//      swapPageInfo->removePage(currPage);
      //swapPageInfo->pageTableEntry = NULL;

        if (currPage->valid == TRUE)
        {
            //int currPID = currPage->space->getPCB()->getPID();
            int currPID = space->getPCB()->getPID();
            DEBUG('v', "E %d: %d\n", currPID, currPage->virtualPage);
            memoryManager->clearPage(currPage->physicalPage);
            physicalMemoryInfo[currPage->physicalPage].space = NULL; 
        }
        swapSectorMap->Clear((currPage->locationOnDisk) / PageSize);
    }
}

/*
 * After selecting a slot of physical memory as a victim and taking care of
 * synchronizing the data if needed, we load the faulting page into memory.
*/
void VirtualMemoryManager::loadPageToCurrVictim(int virtAddr)
{
    int pageTableIndex = virtAddr / PageSize;
    TranslationEntry* page = currentThread->space->getPageTableEntry(pageTableIndex);
    char* physMemLoc = machine->mainMemory + page->physicalPage * PageSize;
    int swapSpaceLoc = page->locationOnDisk;
    swapFile->ReadAt(physMemLoc, PageSize, swapSpaceLoc);

  //  int swapSpaceIndex = swapSpaceLoc / PageSize;
 //   SwapSectorInfo * swapPageInfo = swapSpaceInfo + swapSpaceIndex;
    page->valid = TRUE;
//    swapPageInfo->setValidBit(TRUE);
//    swapPageInfo->setPhysMemPageNum(page->physicalPage);
}

/*
 * Helper function for the second chance page replacement that retrieves the physical page
 * which corresponds to the given physical memory page information that the
 * VirtualMemoryManager maintains.
 * This return page table entry corresponding to a physical page
 */
TranslationEntry* VirtualMemoryManager::getPageTableEntry(FrameInfo * physPageInfo)
{
    TranslationEntry* page = physPageInfo->space->getPageTableEntry(physPageInfo->pageTableIndex);
    return page;
}

void VirtualMemoryManager::copySwapSector(int to, int from)
{
    char sectorBuf[SectorSize];
    swapFile->ReadAt(sectorBuf, SWAP_SECTOR_SIZE, from);
    swapFile->WriteAt(sectorBuf, SWAP_SECTOR_SIZE, to);
}
