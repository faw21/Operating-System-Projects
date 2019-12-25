/*
	FUSE: Filesystem in Userspace
	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

	This program can be distributed under the terms of the GNU GPL.
	See the file COPYING.
*/

#define	FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>

//size of a disk block
#define	BLOCK_SIZE 512

//we'll use 8.3 filenames
#define	MAX_FILENAME 8
#define	MAX_EXTENSION 3

//How many files can there be in one directory?
#define MAX_FILES_IN_DIR (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + (MAX_EXTENSION + 1) + sizeof(size_t) + sizeof(long))

//The attribute packed means to not align these things
struct cs1550_directory_entry
{
	int nFiles;	//How many files are in this directory.
				//Needs to be less than MAX_FILES_IN_DIR

	struct cs1550_file_directory
	{
		char fname[MAX_FILENAME + 1];	//filename (plus space for nul)
		char fext[MAX_EXTENSION + 1];	//extension (plus space for nul)
		size_t fsize;					//file size
		long nStartBlock;				//where the first block is on disk
	} __attribute__((packed)) files[MAX_FILES_IN_DIR];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_FILES_IN_DIR * sizeof(struct cs1550_file_directory) - sizeof(int)];
} ;

typedef struct cs1550_root_directory cs1550_root_directory;

#define MAX_DIRS_IN_ROOT (BLOCK_SIZE - sizeof(int)) / ((MAX_FILENAME + 1) + sizeof(long))

struct cs1550_root_directory
{
	int nDirectories;	//How many subdirectories are in the root
						//Needs to be less than MAX_DIRS_IN_ROOT
	struct cs1550_directory
	{
		char dname[MAX_FILENAME + 1];	//directory name (plus space for nul)
		long nStartBlock;				//where the directory block is on disk
	} __attribute__((packed)) directories[MAX_DIRS_IN_ROOT];	//There is an array of these

	//This is some space to get this to be exactly the size of the disk block.
	//Don't use it for anything.  
	char padding[BLOCK_SIZE - MAX_DIRS_IN_ROOT * sizeof(struct cs1550_directory) - sizeof(int)];
} ;


typedef struct cs1550_directory_entry cs1550_directory_entry;

//How much data can one block hold?
#define	MAX_DATA_IN_BLOCK (BLOCK_SIZE)

struct cs1550_disk_block
{
	//All of the space in the block can be used for actual data
	//storage.
	char data[MAX_DATA_IN_BLOCK];
};

typedef struct cs1550_disk_block cs1550_disk_block;

static FILE* get_disk(void){
	FILE* disk = fopen(".disk", "r+b");
	fseek(disk, 0, SEEK_SET);
	return disk;
}

static cs1550_root_directory get_root(FILE* disk){
	cs1550_root_directory root;
	fread(&root, BLOCK_SIZE, 1, disk);
	return root;
}

static long find_free_block(FILE* disk){

	int seek_ret = fseek(disk, -1* BLOCK_SIZE * 3, SEEK_END);
	if(seek_ret<0){
		return -1;
	}

	unsigned char bits[BLOCK_SIZE*3];

	int read_ret = fread(bits, BLOCK_SIZE*3, 1, disk); 
	if(read_ret <0){
		return -1;
	}

	seek_ret = fseek(disk, -1* BLOCK_SIZE * 3, SEEK_END);
	if(seek_ret<0){
		return -1;
	}

	long i;
	for(i=0; i<BLOCK_SIZE*3; i++){
		unsigned char and = 1;
		int j;
		for(j=0 ; j<8; j++){
			if( (bits[i] & and) == 0){
				bits[i]|=and; // set the bit to 1
				fwrite(bits, BLOCK_SIZE*3, 1, disk);
				fclose(disk);
				return i*8 + j +1;
			}
			and*=2;
		}
	}
	return -1;
}

static int peek_next_free_block(FILE* disk, int blockNum){

	int seek_ret = fseek(disk, -1* BLOCK_SIZE * 3, SEEK_END);
	if(seek_ret<0){
		return -1;
	}

	unsigned char bits[BLOCK_SIZE*3];

	int read_ret = fread(bits, BLOCK_SIZE*3, 1, disk); 
	if(read_ret <0){
		return -1;
	}

	seek_ret = fseek(disk, -1* BLOCK_SIZE * 3, SEEK_END);
	if(seek_ret<0){
		return -1;
	}

	long i;
	for(i=0; i<BLOCK_SIZE*3; i++){
		unsigned char and = 1;
		int j;
		for(j=0 ; j<8; j++){
			if (i*8 + j + 1 == blockNum){
				if( (bits[i] & and) == 0){
				//bits[i]|=and; // set the bit to 1
				//fwrite(bits, BLOCK_SIZE*3, 1, disk);

					return i*8 + j +1;
				}
				else return -1;
			}
			and*=2;
		}
	}
	return -1;
}
/*
 * Called whenever the system wants to know the file attributes, including
 * simply whether the file exists or not. 
 *
 * man -s 2 stat will show the fields of a stat structure
 */
static int cs1550_getattr(const char *path, struct stat *stbuf)
{
	int res = 0;

	

	memset(stbuf, 0, sizeof(struct stat));
   
	//is path the root dir?
	if (strcmp(path, "/") == 0) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	} else {
		char directory[MAX_FILENAME+1], filename[MAX_FILENAME+1], extension[MAX_EXTENSION+1];
		memset(directory, 0, MAX_FILENAME+1);
		memset(filename, 0, MAX_FILENAME+1);
		memset(extension, 0, MAX_EXTENSION+1);

		int filled = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
		if(filled >= 1){ //if only the directory is filled, the path represents a subdir.
			
			int i;
			struct cs1550_directory subdir;
			strcpy(subdir.dname, "");
			FILE* disk = get_disk();
			cs1550_root_directory root = get_root(disk);

			for(i = 0; i < root.nDirectories; i++){ //Find the subdirectory in the root's array of directories
				if(strcmp(root.directories[i].dname, directory) == 0){ //This current directory and our search directory names match
					subdir = root.directories[i];
					break;
				}
			}
			if(strcmp(subdir.dname, "")==0) res = -ENOENT;
			else if (filled == 1){
				stbuf->st_mode = S_IFDIR | 0755;
				stbuf->st_nlink = 2;
				res = 0;
			} else {
				int location_on_disk = BLOCK_SIZE*subdir.nStartBlock;
				fseek(disk, location_on_disk, SEEK_SET);
				cs1550_directory_entry dir_entry;
				if(fread(&dir_entry, BLOCK_SIZE, 1, disk)==1){
					struct cs1550_file_directory file;
					strcpy(file.fname, "");
					off_t filesize = 0;
					for (i = 0; i < MAX_FILES_IN_DIR; i++){
						if (strcmp(dir_entry.files[i].fname, filename) == 0
							&& strcmp(dir_entry.files[i].fext, extension) == 0){
							
							file = dir_entry.files[i];
							filesize = file.fsize;
							break;
						}
					}
					if(strcmp(file.fname, "")==0){
						res = -ENOENT;
					} else {
						stbuf->st_mode = S_IFREG | 0666; 
						stbuf->st_nlink = 1;
						stbuf->st_size = filesize;
						res = 0;
					}
				} else {
					res = -ENOENT;
				}
			}
			

			fclose(disk);
		
		} else {
			res = -ENOENT;
		}
	}
	return res;
}

/* 
 * Called whenever the contents of a directory are desired. Could be from an 'ls'
 * or could even be when a user hits TAB to do autocompletion
 */
static int cs1550_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			 off_t offset, struct fuse_file_info *fi)
{
	int res = 0;
	//Since we're building with -Wall (all warnings reported) we need
	//to "use" every parameter, so let's just cast them to void to
	//satisfy the compiler
	(void) offset;
	(void) fi;
	//the filler function allows us to add entries to the listing
	//read the fuse.h file for a description (in the ../include dir)
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);


	FILE* disk = get_disk();
	cs1550_root_directory root = get_root(disk);
	


	if (strcmp(path, "/") == 0){
		
		int i;
		for(i = 0; i < MAX_DIRS_IN_ROOT; i++){ 
			if(strcmp(root.directories[i].dname, "") != 0){
				filler(buf, root.directories[i].dname, NULL, 0);
			}
		}
	} else {
		char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
		int filled = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
		if (filled == 1) {
			struct cs1550_directory subdir; 
			strcpy(subdir.dname, "");
			subdir.nStartBlock = -1;

			int i;
			for(i = 0; i < MAX_DIRS_IN_ROOT; i++){ 
				if(strcmp(root.directories[i].dname, directory) == 0){
					subdir = root.directories[i];
					break;
				}
			}

			if(strcmp(subdir.dname, "")==0) res = -ENOENT;
			else {
				int location_on_disk = subdir.nStartBlock*BLOCK_SIZE;
				fseek(disk, location_on_disk, SEEK_SET);

				cs1550_directory_entry directory;
				directory.nFiles = 0;
				fread(&directory, BLOCK_SIZE, 1, disk);

				for (i = 0; i < directory.nFiles; i++){
					char newFileName[MAX_FILENAME+1];
					strcpy(newFileName, directory.files[i].fname);
					strcat(newFileName, ".");
					strcat(newFileName, directory.files[i].fext);
					filler(buf, newFileName, NULL, 0);
				}
			}
		} else {
			res = -ENOENT;
		}
	}

	/*
	//add the user stuff (subdirs or files)
	//the +1 skips the leading '/' on the filenames
	filler(buf, newpath + 1, NULL, 0);
	*/
	return res;
}

/* 
 * Creates a directory. We can ignore mode since we're not dealing with
 * permissions, as long as getattr returns appropriate ones for us.
 */
static int cs1550_mkdir(const char *path, mode_t mode)
{
	(void) mode;
	//(void) path;
	//return 0;
	int res = 0;


	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	int filled = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	if (strcmp(path, "/")==0 || filled>1){
		res = -EPERM;
	} else {
		if (strlen(directory) > MAX_FILENAME){
			res = -ENAMETOOLONG;
		}
		FILE* disk = get_disk();
		cs1550_root_directory root = get_root(disk);

		int i;
		for (i = 0; i < MAX_DIRS_IN_ROOT; i++){
			if (strcmp(root.directories[i].dname, directory)==0){
				return -EEXIST;
			}
		}

		for (i = 0; i < MAX_DIRS_IN_ROOT; i++){
			if (strcmp(root.directories[i].dname, "")==0){
				strcpy(root.directories[i].dname, directory);
				int blockNum = find_free_block(disk);
				root.directories[i].nStartBlock = blockNum;
				root.nDirectories++;
				break;
			}
		}

		cs1550_directory_entry new_dir;
		new_dir.nFiles = 0;
		disk = fopen(".disk", "rb+");
		fseek(disk, BLOCK_SIZE * root.directories[i].nStartBlock , SEEK_SET);
		fwrite((void*)&new_dir, BLOCK_SIZE, 1, disk);
		fseek(disk, 0, SEEK_SET);
		fwrite((void*)&root, BLOCK_SIZE, 1, disk);
		fseek(disk, 0, SEEK_SET);
		fclose(disk);
		res = 0;
	}
	return res;
}

/* 
 * Removes a directory.
 */
static int cs1550_rmdir(const char *path)
{
	(void) path;
    return 0;
}

/* 
 * Does the actual creation of a file. Mode and dev can be ignored.
 *
 */
static int cs1550_mknod(const char *path, mode_t mode, dev_t dev)
{
	(void) mode;
	(void) dev;
	//return 0;

	int res = 0;

	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	int filled = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);
	if (strcmp(path, "/")==0 || filled < 3){
		res = -EPERM;
	} else {
		if (strlen(directory) > MAX_FILENAME || strlen(filename) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION){
			res = -ENAMETOOLONG;
		}
		FILE* disk = get_disk();
		cs1550_root_directory root = get_root(disk);

		struct cs1550_directory dir;
		strcpy(dir.dname, "");
		int i;
		for (i = 0; i < MAX_DIRS_IN_ROOT; i++){
			if (strcmp(root.directories[i].dname, directory)==0){
				dir = root.directories[i];
			}
		}
		if (strcmp(dir.dname, "")==0){
			return -ENOENT;
		}

		int location_on_disk = BLOCK_SIZE*dir.nStartBlock;
		fseek(disk, location_on_disk, SEEK_SET);
		cs1550_directory_entry dir_entry;

		if(fread(&dir_entry, BLOCK_SIZE, 1, disk)==1){


			for (i = 0; i < MAX_FILES_IN_DIR; i++){
				if (strcmp(dir_entry.files[i].fname, filename) == 0
					&& strcmp(dir_entry.files[i].fext, extension) == 0){
					return -EEXIST;
				}
			}

			for (i = 0; i < MAX_FILES_IN_DIR; i++){
				if (strcmp(dir_entry.files[i].fname, "") == 0
					&& strcmp(dir_entry.files[i].fext, "") == 0){

					strcpy(dir_entry.files[i].fname, filename);
					strcpy(dir_entry.files[i].fext, extension);
					int blockNum = find_free_block(disk);
					dir_entry.files[i].nStartBlock = blockNum;
					dir_entry.files[i].fsize = 0;
					dir_entry.nFiles++;
					break;
				}

				
			}


			disk = fopen(".disk", "rb+");
			fseek(disk, BLOCK_SIZE * dir_entry.files[i].nStartBlock , SEEK_SET);
			char temp[BLOCK_SIZE];
			fwrite((void*)&temp, BLOCK_SIZE, 1, disk);
			//struct cs1550_disk_block diskblock;
			//fwrite((void*)&diskblock, BLOCK_SIZE, 1, disk);

			fseek(disk, location_on_disk, SEEK_SET);
			fwrite((void*)&dir_entry, BLOCK_SIZE, 1, disk);

			fseek(disk, 0, SEEK_SET);
			fclose(disk);
			res = 0;

		} else {
			res = -ENOENT;
		}

		
	}
	return res;
}

/*
 * Deletes a file
 */
static int cs1550_unlink(const char *path)
{
    (void) path;

    return 0;
}

/* 
 * Read size bytes from file into buf starting from offset
 *
 */
static int cs1550_read(const char *path, char *buf, size_t size, off_t offset,
			  struct fuse_file_info *fi)
{
	(void) offset;
	(void) fi;

	if (size<=0) return -1;

	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	int filled = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	if (strcmp(path, "/")==0){
		return -EPERM;
	} else if (filled < 3) {
		return -EISDIR;
	} else {
		if (strlen(directory) > MAX_FILENAME || strlen(filename) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION){
			return -ENAMETOOLONG;
		}
		FILE* disk = get_disk();
		cs1550_root_directory root = get_root(disk);

		struct cs1550_directory dir;
		strcpy(dir.dname, "");
		int i;
		for (i = 0; i < MAX_DIRS_IN_ROOT; i++){
			if (strcmp(root.directories[i].dname, directory)==0){
				dir = root.directories[i];
			}
		}
		if (strcmp(dir.dname, "")==0){
			return -ENOENT;
		}

		int location_on_disk = BLOCK_SIZE*dir.nStartBlock;
		fseek(disk, location_on_disk, SEEK_SET);
		cs1550_directory_entry dir_entry;
		struct cs1550_file_directory file;
		strcpy(file.fname, "");
		file.nStartBlock = -1;
		file.fsize = 0;

		if(fread(&dir_entry, BLOCK_SIZE, 1, disk)==1){



			for (i = 0; i < MAX_FILES_IN_DIR; i++){
				if (strcmp(dir_entry.files[i].fname, filename) == 0
					&& strcmp(dir_entry.files[i].fext, extension) == 0){
					
					file = dir_entry.files[i];
					file.fsize = dir_entry.files[i].fsize;
				}
			}

			if (strcmp(file.fname, "")==0){
				return -ENOENT;
			}

		}

		if(file.fsize<offset){
			return -EFBIG;
		}

		//int byte_in_block = offset % MAX_DATA_IN_BLOCK;
		fseek(disk, BLOCK_SIZE*file.nStartBlock+offset, SEEK_SET);
		if(file.fsize<offset+size) size = file.fsize - offset;
		char *read = malloc(size);
		fread(read, size, 1, disk);
		memcpy(buf, read, size);
		fseek(disk, 0, SEEK_SET);
		fclose(disk);
	}

	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//read in data
	//set size and return, or error


	return size;
}

/* 
 * Write size bytes from buf into file starting from offset
 *
 */
static int cs1550_write(const char *path, const char *buf, size_t size, 
			  off_t offset, struct fuse_file_info *fi)
{

	(void) buf;
	(void) offset;
	(void) fi;


	if (size<=0) return -1;

	char directory[MAX_FILENAME + 1], filename[MAX_FILENAME + 1], extension[MAX_EXTENSION + 1];
	int filled = sscanf(path, "/%[^/]/%[^.].%s", directory, filename, extension);

	if (strcmp(path, "/")==0 || filled < 3){
		return -EPERM;
	} else {
		if (strlen(directory) > MAX_FILENAME || strlen(filename) > MAX_FILENAME || strlen(extension) > MAX_EXTENSION){
			return -ENAMETOOLONG;
		}
		FILE* disk = get_disk();
		cs1550_root_directory root = get_root(disk);

		struct cs1550_directory dir;
		strcpy(dir.dname, "");
		int i;
		for (i = 0; i < MAX_DIRS_IN_ROOT; i++){
			if (strcmp(root.directories[i].dname, directory)==0){
				dir = root.directories[i];
			}
		}
		if (strcmp(dir.dname, "")==0){
			return -ENOENT;
		}

		int location_on_disk = BLOCK_SIZE*dir.nStartBlock;
		fseek(disk, location_on_disk, SEEK_SET);
		cs1550_directory_entry dir_entry;
		struct cs1550_file_directory file;
		strcpy(file.fname, "");
		file.nStartBlock = -1;
		file.fsize = 0;

		if(fread(&dir_entry, BLOCK_SIZE, 1, disk)==1){


			for (i = 0; i < MAX_FILES_IN_DIR; i++){
				if (strcmp(dir_entry.files[i].fname, filename) == 0
					&& strcmp(dir_entry.files[i].fext, extension) == 0){
					
					file = dir_entry.files[i];
				}
			}

			if (strcmp(file.fname, "")==0){
				return -ENOENT;
			}

		}

		if(file.fsize<offset){
			return -EFBIG;
		}

		int blocks_written = (int)(file.fsize/MAX_DATA_IN_BLOCK);
		int blocks_to_be_written = (int)((offset + size)/MAX_DATA_IN_BLOCK);
		if(blocks_to_be_written > blocks_written){
			if (peek_next_free_block(disk, file.nStartBlock + blocks_written + 1 )== -1){
				return -EFBIG;
			}
			else {
				int j;
				for(j = 0; j < blocks_to_be_written - blocks_written; j++){
					find_free_block(disk);
				}
			}
		}
		if(i<MAX_FILES_IN_DIR)
			dir_entry.files[i].fsize = offset+size;
		
		//int byte_in_block = offset % MAX_DATA_IN_BLOCK;
		fseek(disk, BLOCK_SIZE*file.nStartBlock+offset, SEEK_SET);
		fwrite((void*)buf, size,1,disk);

		fseek(disk, location_on_disk, SEEK_SET);
		fwrite(&dir_entry, BLOCK_SIZE, 1, disk);
		fseek(disk, location_on_disk, SEEK_SET);
		struct cs1550_directory_entry new_entry;
		if(fread(&new_entry, BLOCK_SIZE, 1, disk)==1)
		fclose(disk);
	
	}
		


		/*file.fsize = offset + size;
		
		int byte_in_block = offset % MAX_DATA_IN_BLOCK;
		i=0;
		  while((i+byte_in_block)%MAX_DATA_IN_BLOCK!=0||i==0){
		    file_block.data[(i+byte_in_block)%MAX_DATA_IN_BLOCK]=buf[i];
		    i++;
		    if(i>size)
		      break;
		  }
		 fseek(disk, BLOCK_SIZE*file.nStartBlock + (int)(offset / MAX_DATA_IN_BLOCK), SEEK_SET);
	  	fwrite((void*)&file, sizeof(cs1550_disk_block),1,disk);

	  	if((int)(offset + size)/MAX_DATA_IN_BLOCK >0){
	  		int blocks = 0;
	  		while(true){

	  			byte_in_block = 0;
	  			memset(file_block, 0, sizeof(struct cs1550_disk_block));
	  			i=0;
		  		while((i+byte_in_block)%MAX_DATA_IN_BLOCK!=0||i==0){
		    		file_block.data[(i+byte_in_block)%MAX_DATA_IN_BLOCK]=buf[i];
		    		i++;
		    		if(i>size)
		      			break;
		  		}
	  	}*/









	//check to make sure path exists
	//check that size is > 0
	//check that offset is <= to the file size
	//write data
	//set size (should be same as input) and return, or error

	return size;
}

/******************************************************************************
 *
 *  DO NOT MODIFY ANYTHING BELOW THIS LINE
 *
 *****************************************************************************/

/*
 * truncate is called when a new file is created (with a 0 size) or when an
 * existing file is made shorter. We're not handling deleting files or 
 * truncating existing ones, so all we need to do here is to initialize
 * the appropriate directory entry.
 *
 */
static int cs1550_truncate(const char *path, off_t size)
{
	(void) path;
	(void) size;

    return 0;
}


/* 
 * Called when we open a file
 *
 */
static int cs1550_open(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;
    /*
        //if we can't find the desired file, return an error
        return -ENOENT;
    */

    //It's not really necessary for this project to anything in open

    /* We're not going to worry about permissions for this project, but 
	   if we were and we don't have them to the file we should return an error

        return -EACCES;
    */

    return 0; //success!
}

/*
 * Called when close is called on a file descriptor, but because it might
 * have been dup'ed, this isn't a guarantee we won't ever need the file 
 * again. For us, return success simply to avoid the unimplemented error
 * in the debug log.
 */
static int cs1550_flush (const char *path , struct fuse_file_info *fi)
{
	(void) path;
	(void) fi;

	return 0; //success!
}


//register our new functions as the implementations of the syscalls
static struct fuse_operations hello_oper = {
    .getattr	= cs1550_getattr,
    .readdir	= cs1550_readdir,
    .mkdir	= cs1550_mkdir,
	.rmdir = cs1550_rmdir,
    .read	= cs1550_read,
    .write	= cs1550_write,
	.mknod	= cs1550_mknod,
	.unlink = cs1550_unlink,
	.truncate = cs1550_truncate,
	.flush = cs1550_flush,
	.open	= cs1550_open,
};

//Don't change this.
int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &hello_oper, NULL);
}
