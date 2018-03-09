// filesys.cc 
//	Routines to manage the overall operation of the file system.
//	Implements routines to map from textual file names to files.
//
//	Each file in the file system has:
//	   A file header, stored in a sector on disk 
//		(the size of the file header data structure is arranged
//		to be precisely the size of 1 disk sector)
//	   A number of data blocks
//	   An entry in the file system directory
//
// 	The file system consists of several data structures:
//	   A bitmap of free disk sectors (cf. bitmap.h)
//	   A directory of file names and file headers
//
//      Both the bitmap and the directory are represented as normal
//	files.  Their file headers are located in specific sectors
//	(sector 0 and sector 1), so that the file system can find them 
//	on bootup.
//
//	The file system assumes that the bitmap and directory files are
//	kept "open" continuously while Nachos is running.
//
//	For those operations (such as Create, Remove) that modify the
//	directory and/or bitmap, if the operation succeeds, the changes
//	are written immediately back to disk (the two files are kept
//	open during all this time).  If the operation fails, and we have
//	modified part of the directory and/or bitmap, we simply discard
//	the changed version, without writing it back to disk.
//
// 	Our implementation at this point has the following restrictions:
//
//	   there is no synchronization for concurrent accesses
//	   files have a fixed size, set when the file is created
//	   files cannot be bigger than about 3KB in size
//	   there is no hierarchical directory structure, and only a limited
//	     number of files can be added to the system
//	   there is no attempt to make the system robust to failures
//	    (if Nachos exits in the middle of an operation that modifies
//	    the file system, it may corrupt the disk)
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation 
// of liability and disclaimer of warranty provisions.
#ifndef FILESYS_STUB

#include "copyright.h"
#include "debug.h"
#include "disk.h"
#include "pbitmap.h"
#include "directory.h"
#include "filehdr.h"
#include "filesys.h"

// Sectors containing the file headers for the bitmap of free sectors,
// and the directory of files.  These file headers are placed in well-known 
// sectors, so that they can be located on boot-up.
#define FreeMapSector 		0
#define DirectorySector 	1

// Initial file sizes for the bitmap and directory; until the file system
// supports extensible files, the directory size sets the maximum number 
// of files that can be loaded onto the disk.
#define FreeMapFileSize 	(NumSectors / BitsInByte)
//MP4: extend to '64' files/subdirectories per directory
#define NumDirEntries 		64
#define DirectoryFileSize 	(sizeof(DirectoryEntry) * NumDirEntries)

//----------------------------------------------------------------------
// FileSystem::FileSystem
// 	Initialize the file system.  If format = TRUE, the disk has
//	nothing on it, and we need to initialize the disk to contain
//	an empty directory, and a bitmap of free sectors (with almost but
//	not all of the sectors marked as free).  
//
//	If format = FALSE, we just have to open the files
//	representing the bitmap and the directory.
//
//	"format" -- should we initialize the disk?
//----------------------------------------------------------------------

FileSystem::FileSystem(bool format)
{ 
    DEBUG(dbgFile, "Initializing the file system.");
    if (format) {
        PersistentBitmap *freeMap = new PersistentBitmap(NumSectors);
        Directory *directory = new Directory(NumDirEntries);
		FileHeader *mapHdr = new FileHeader;
		FileHeader *dirHdr = new FileHeader;

        DEBUG(dbgFile, "Formatting the file system.");

		// First, allocate space for FileHeaders for the directory and bitmap
		// (make sure no one else grabs these!)
		freeMap->Mark(FreeMapSector);	    
		freeMap->Mark(DirectorySector);

		// Second, allocate space for the data blocks containing the contents
		// of the directory and bitmap files.  There better be enough space!

		ASSERT(mapHdr->Allocate(freeMap, FreeMapFileSize));
		ASSERT(dirHdr->Allocate(freeMap, DirectoryFileSize));

		// Flush the bitmap and directory FileHeaders back to disk
		// We need to do this before we can "Open" the file, since open
		// reads the file header off of disk (and currently the disk has garbage
		// on it!).

        DEBUG(dbgFile, "Writing headers back to disk.");
		mapHdr->WriteBack(FreeMapSector);    
		dirHdr->WriteBack(DirectorySector);

		// OK to open the bitmap and directory files now
		// The file system operations assume these two files are left open
		// while Nachos is running.

        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
     
		// Once we have the files "open", we can write the initial version
		// of each file back to disk.  The directory at this point is completely
		// empty; but the bitmap has been changed to reflect the fact that
		// sectors on the disk have been allocated for the file headers and
		// to hold the file data for the directory and bitmap.

        DEBUG(dbgFile, "Writing bitmap and directory back to disk.");
		freeMap->WriteBack(freeMapFile);	 // flush changes to disk
		directory->WriteBack(directoryFile);

		if (debug->IsEnabled('f')) {
			freeMap->Print();
			directory->Print();
        }
        delete freeMap; 
		delete directory; 
		delete mapHdr; 
		delete dirHdr;
    } else {
		// if we are not formatting the disk, just open the files representing
		// the bitmap and directory; these are left open while Nachos is running
        freeMapFile = new OpenFile(FreeMapSector);
        directoryFile = new OpenFile(DirectorySector);
    }
    for(int i=0;i<20;i++){
        fileDescriptorTable[i] = NULL;
    }
    top = 0;
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileSystem::~FileSystem
//----------------------------------------------------------------------
FileSystem::~FileSystem()
{
	delete freeMapFile;
	delete directoryFile;
    for(int i=0;i<top;i++){
        fileDescriptorTable[i] = NULL;
    }
}

//----------------------------------------------------------------------
// FileSystem::Create
// 	Create a file in the Nachos file system (similar to UNIX create).
//	Since we can't increase the size of files dynamically, we have
//	to give Create the initial size of the file.
//
//	The steps to create a file are:
//	  Make sure the file doesn't already exist
//        Allocate a sector for the file header
// 	  Allocate space on disk for the data blocks for the file
//	  Add the name to the directory
//	  Store the new file header on disk 
//	  Flush the changes to the bitmap and the directory back to disk
//
//	Return TRUE if everything goes ok, otherwise, return FALSE.
//
// 	Create fails if:
//   		file is already in directory
//	 	no free space for file header
//	 	no free entry for file in directory
//	 	no free space for data blocks for the file 
//
// 	Note that this implementation assumes there is no concurrent access
//	to the file system!
//
//	"name" -- name of file to be created
//	"initialSize" -- size of file to be created
//----------------------------------------------------------------------

bool
FileSystem::Create(char *pathName, int initialSize, bool isDir)
{
    Directory *directory;
    PersistentBitmap *freeMap;
    FileHeader *hdr;
    int sector;
    bool success;

    //MP4
    if(isDir)   initialSize = DirectoryFileSize;
    //check path length <= 255
    if(strlen(pathName) > 255){
        printf("Path %s exceeds Max path length 255\n", pathName);
        return FALSE;
    }

    DEBUG(dbgFile, "Creating file " << pathName << " size " << initialSize); 

    //MP4
    //find the target file's directory
    char name[1024], buf[1024];
    //get the true file/dir name
    getFileName(name, pathName);
    strcpy(buf, pathName);
    //strcpy(name, pathName);
    //OpenFile *curDirFile = getSubDir(name);
    OpenFile *curDirFile = getSubDir(buf);
    if(curDirFile==NULL){   //directory not found or just root
        return FALSE;
    }

    directory = new Directory(NumDirEntries);
    directory->FetchFrom(curDirFile);

    //after getSubDir() process, here 'name' is already be the file name only
    if (directory->Find(name) != -1 || strlen(name) > 9){
      success = FALSE;			// file is already in directory
    }
    else {	
        freeMap = new PersistentBitmap(freeMapFile,NumSectors);
        sector = freeMap->FindAndSet();	// find a sector to hold the file header
    	if (sector == -1)	
            success = FALSE;		// no free block for file header 
        else if (!directory->Add(name, sector, isDir))
            success = FALSE;	// no space in directory
	else {
    	    hdr = new FileHeader;
	    if (!hdr->Allocate(freeMap, initialSize))
            	success = FALSE;	// no space on disk for data
	    else {	
	    	success = TRUE;
		// everthing worked, flush all changes back to disk
    	    	hdr->WriteBack(sector);
                //MP4: store back to 'curDirFile' 		
    	    	directory->WriteBack(curDirFile);
    	    	freeMap->WriteBack(freeMapFile);
            
            //MP4: if this file is "DIR" -> initialize directory structure
            Directory *dir = new Directory(NumDirEntries);
            OpenFile *dirFile = new OpenFile(sector);
            dir->WriteBack(dirFile);
            delete dirFile;
            delete dir;
	    }
            delete hdr;
	}
        delete freeMap;
    }
    //remember to delete curDirFile, if it's not root
    if(curDirFile!=NULL && curDirFile!=directoryFile)   delete curDirFile;

    delete directory;
    return success;
}

//----------------------------------------------------------------------
// FileSystem::Open
// 	Open a file for reading and writing.  
//	To open a file:
//	  Find the location of the file's header, using the directory 
//	  Bring the header into memory
//
//	"name" -- the text name of the file to be opened
//----------------------------------------------------------------------

OpenFile *
FileSystem::Open(char *pathName)
{ 
    OpenFile *openFile = NULL;
    int sector;

    DEBUG(dbgFile, "Opening file" << pathName);

    //MP4
    char name[1024], buf[1024];
    getFileName(name, pathName);
    strcpy(buf, pathName);
    //strcpy(name, pathName);
    //OpenFile *curDirFile = getSubDir(name);
    OpenFile *curDirFile = getSubDir(buf);
    if(curDirFile==NULL){   //file not found
        return NULL;
    }



    Directory *directory = new Directory(NumDirEntries);
    directory->FetchFrom(curDirFile);

    //cout<<"Open File "<<name<<endl;
    //printf("Open File %s\n", name);
    sector = directory->Find(name); 

    //MP4
    //at most 20 file opened at a time
    if(top>=20){
        if(curDirFile!=NULL && curDirFile!=directoryFile)   delete curDirFile;
        delete directory;
        return NULL;
    }

    if (sector >= 0){
        openFile = new OpenFile(sector);// name was found in directory 
        fileDescriptorTable[top++] = openFile;
    }

    //remember to delete curDirFile, if it's not root
    if(curDirFile!=NULL && curDirFile!=directoryFile)   delete curDirFile;
		
    delete directory;
    return openFile;				// return NULL if not found
}

//----------------------------------------------------------------------
// FileSystem::Remove
// 	Delete a file from the file system.  This requires:
//	    Remove it from the directory
//	    Delete the space for its header
//	    Delete the space for its data blocks
//	    Write changes to directory, bitmap back to disk
//
//	Return TRUE if the file was deleted, FALSE if the file wasn't
//	in the file system.
//
//	"name" -- the text name of the file to be removed
//----------------------------------------------------------------------

bool
FileSystem::Remove(bool recursive, char *pathName)
{ 
    Directory *directory;
    PersistentBitmap *freeMap;
    FileHeader *fileHdr;
    int sector;

    //MP4
    char name[1024], buf[1024];
    getFileName(name, pathName);
    strcpy(buf, pathName);
    //strcpy(name, pathName);
    //OpenFile *curDirFile = getSubDir(name);
    OpenFile *curDirFile = getSubDir(buf);
    if(curDirFile==NULL){
        return NULL;
    }
    
    directory = new Directory(NumDirEntries);
    directory->FetchFrom(curDirFile);

    sector = directory->Find(name);
    if (sector == -1) {
       //MP4
       if(curDirFile!=NULL && curDirFile!=directoryFile)   delete curDirFile;
       delete directory;
       return FALSE;			 // file not found 
    }
    if(directory->isDir(name)){
        //cout<<"Remove Dir "<<name<<endl;
        printf("Remove Dir %s\n", name);
    }
    else
        printf("Remove File %s\n", name);

    //MP4 bonus: recursive remove a directory
    //PS: target dir will 'never' be the root
    if(directory->isDir(name) && recursive){
        OpenFile *targetDirFile = new OpenFile(sector);
        Directory *targetDir = new Directory(NumDirEntries);
        targetDir->FetchFrom(targetDirFile);

        for(int i=0;i<targetDir->tableSize;i++){
            if(targetDir->table[i].inUse){
                //modified the path -> add addition one '/', ready for appending filename at the back
                char targetPath[1024];
                strcpy(targetPath, pathName);
                int len = strlen(targetPath);
                targetPath[len] = '/';
                //append the file at the back
                strcpy(targetPath+len+1, targetDir->table[i].name);
                Remove(recursive, targetPath);
            }
        }
        delete targetDir;
        delete targetDirFile;
    }
    fileHdr = new FileHeader;
    fileHdr->FetchFrom(sector);

    freeMap = new PersistentBitmap(freeMapFile,NumSectors);

    fileHdr->Deallocate(freeMap);  		// remove data blocks
    freeMap->Clear(sector);			// remove header block
    directory->Remove(name);

    freeMap->WriteBack(freeMapFile);		// flush to disk
    //MP4: to 'curDir'
    directory->WriteBack(curDirFile);        // flush to disk

    //remember to delete curDirFile, if it's not root
    if(curDirFile!=NULL && curDirFile!=directoryFile)   delete curDirFile;

    delete fileHdr;
    delete directory;
    delete freeMap;
    return TRUE;
} 

//----------------------------------------------------------------------
// FileSystem::List
// 	List all the files in the file system directory.
//----------------------------------------------------------------------

void
FileSystem::List(bool recursive, char *listDirPath)
{   
    //MP4
    //case: list root
    if(!strcmp(listDirPath, "/")){
        //cout<<"List directory: [/] (root)"<<endl;
        //printf("List directory: [/] (root)\n");
        Directory *directory = new Directory(NumDirEntries);
        directory->FetchFrom(directoryFile);
        directory->List(recursive, 0);
        delete directory;
        return;
    }

    char name[1024], buf[1024];
    getFileName(name, listDirPath);
    strcpy(buf, listDirPath);
    //strcpy(name, listDirPath);
    //OpenFile *curDirFile = getSubDir(name);
    OpenFile *curDirFile = getSubDir(buf);
    if(curDirFile==NULL){
        return;
    }

    Directory *directory = new Directory(NumDirEntries);
    directory->FetchFrom(curDirFile);

    //find the target directory 
    int sector = directory->Find(name);
    if(sector!=-1){
        //cout<<"List directory: [" << name<< "]" <<endl;
        //printf("List directory: [%s]\n", name);
        OpenFile *tmp = new OpenFile(sector);
        Directory *targetDir = new Directory(NumDirEntries);
        targetDir->FetchFrom(tmp);
        targetDir->List(recursive, 0);
        delete targetDir;
        delete tmp;
    }

    //remember to delete curDirFile, if it's not root
    if(curDirFile!=NULL && curDirFile!=directoryFile)   delete curDirFile;
    delete directory;
}

//----------------------------------------------------------------------
// FileSystem::Print
// 	Print everything about the file system:
//	  the contents of the bitmap
//	  the contents of the directory
//	  for each file in the directory,
//	      the contents of the file header
//	      the data in the file
//----------------------------------------------------------------------

void
FileSystem::Print()
{
    FileHeader *bitHdr = new FileHeader;
    FileHeader *dirHdr = new FileHeader;
    PersistentBitmap *freeMap = new PersistentBitmap(freeMapFile,NumSectors);
    Directory *directory = new Directory(NumDirEntries);

    printf("Bit map file header:\n");
    bitHdr->FetchFrom(FreeMapSector);
    bitHdr->Print();

    printf("Directory file header:\n");
    dirHdr->FetchFrom(DirectorySector);
    dirHdr->Print();

    freeMap->Print();

    directory->FetchFrom(directoryFile);
    directory->Print();

    delete bitHdr;
    delete dirHdr;
    delete freeMap;
    delete directory;
}

//MP4
OpenFile* FileSystem::getSubDir(char *pathName)
{
    Directory *curDir = new Directory(NumDirEntries);
    OpenFile *curDirFile = directoryFile;
    curDir->FetchFrom(curDirFile);

    //method: use strtok() function to find out '/' in the path
    char *cut = strtok(pathName, "/");
    if(cut==NULL){  //just the root
        delete curDir;
        return NULL;
    }
    //loop search the subdirectory location
    //notice: first 'cut' is already the first 'sub'directory
    while(TRUE){
        char *nextcut = strtok(NULL, "/");
        if(nextcut==NULL){  //no subdirectory find
            //strcpy(pathName, cut);
            delete curDir;
            return curDirFile;
        }else{
            int subDirSector = curDir->Find(cut);
            if(subDirSector==-1){
                printf("ERROR: The target SubDirectory %s is not found.\n", cut);
                delete curDir;
                //remember delete 'in-core' data if not the root directory file
                if(curDirFile!=directoryFile){
                    delete curDirFile;
                }
                return NULL;
            }
            //delete old curDirFile, update with new found subdirectory
            if(curDirFile!=directoryFile){
                delete curDirFile;
            }
            curDirFile = new OpenFile(subDirSector);
            curDir->FetchFrom(curDirFile);
            cut = nextcut;
        }
    }

}
void FileSystem::getFileName(char *result, char *pathName)
{
    memset(result, '\0', 1024);
    int idx = strlen(pathName) - 1;
    while(idx >0 && pathName[idx]!='/'){
        --idx;
    }
    strcpy(result, pathName+idx+1);
}

#endif // FILESYS_STUB

