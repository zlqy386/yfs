// yfs client.  implements FS operations using extent and lock server
#include "yfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

yfs_client::yfs_client(std::string extent_dst, std::string lock_dst)
{
  ec = new extent_client(extent_dst);
  lc = new lock_client_cache(lock_dst);
  //lc = new lock_client(lock_dst);
  if (ec->put(1, "") != extent_protocol::OK)
      printf("error init root dir\n"); // XYB: init root dir
}


yfs_client::inum
yfs_client::n2i(std::string n)
{
    std::istringstream ist(n);
    unsigned long long finum;
    ist >> finum;
    return finum;
}

std::string
yfs_client::filename(inum inum)
{
    std::ostringstream ost;
    ost << inum;
    return ost.str();
}

bool
yfs_client::isfile(inum inum)
{
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_FILE) {
        printf("isfile: %lld is a file\n", inum);
        return true;
    } 
    printf("isfile: %lld is a dir\n", inum);
    return false;
}

bool
yfs_client::isdir(inum inum)
{
    return ! isfile(inum);
}

int
yfs_client::getfile(inum inum, fileinfo &fin)
{
    int r = OK;

    printf("getfile %016llx \n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    fin.atime = a.atime;
    fin.mtime = a.mtime;
    fin.ctime = a.ctime;
    fin.size = a.size;
    printf("getfile %016llx -> sz %llu\n", inum, fin.size);

release:
    return r;
}

int
yfs_client::getdir(inum inum, dirinfo &din)
{
    int r = OK;

    printf("getdir %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }
    din.atime = a.atime;
    din.mtime = a.mtime;
    din.ctime = a.ctime;

release:
    return r;
}


#define EXT_RPC(xx) do { \
    if ((xx) != extent_protocol::OK) { \
        printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
        r = IOERR; \
        goto release; \
    } \
} while (0)

// Only support set size of attr
int
yfs_client::setattr(inum ino, size_t size)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: get the content of inode ino, and modify its content
     * according to the size (<, =, or >) content length.
     */
	
	std::string buf;
	ec->get(ino, buf);
	if (size < buf.size()) {
		buf = buf.substr(0, size);
	} else if (size > buf.size()) {
		for (size_t i = buf.size(); i < size; i++) {
			buf.append("\0");
		}
	}
	ec->put(ino, buf);
	
    return r;
}

int
yfs_client::create_(inum parent, const char *name, mode_t mode, inum &ino_out, extent_protocol::types type)
{
    int r = OK;
    /*
     * your lab2 code goes here.
     * note: lookup is what you need to check if file exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */
	
	bool found;
	inum ino_lookup = 0;
	r = lookup(parent, name, found, ino_lookup);
	if (r != OK) {
      return r;
	}
	if (found) {
	  r = EXIST;
      return r;
	}
	r = ec->create(type, ino_out);
	if (r != OK) {
      return r;
	}
	r = ec->put(ino_out,"");
	if (r != OK) {
      return r;
	}
	std::string buf;
	r = ec->get(parent, buf);
	if (r != OK) {
      return r;
	}
	buf += ",";
	buf += name;
	printf("create: NAME:%s INO:%d\n", name, ino_out);//DEBUG
	buf += ",";
	buf += filename(ino_out);
	r = ec->put(parent, buf);
	if (r != OK) {
      return r;
	}

    return r;
}

int
yfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out, extent_protocol::types type)
{
    int r;

	lc->acquire(parent);
	printf("create:lock acquire - inum: %d \n", parent);
	r = create_(parent, name, mode, ino_out, type);
	lc->release(parent);
	printf("create:lock release - inum: %d \n", parent);
    return r;
}

int
yfs_client::lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
    int r = OK;
	
    /*
     * your lab2 code goes here.
     * note: lookup file from parent dir according to name;
     * you should design the format of directory content.
     */
	
	std::list<dirent> list;
	r = readdir(parent, list);
	if (r != OK) {
	  return r;
	}
	found = false;
	for (std::list<dirent>::iterator it = list.begin(); it != list.end(); it++){
      if((*it).name==name){
	    ino_out = (*it).inum;
		found = true;
		break;
	  }
	  printf("\nname:%s\n",name);
	}

    return r;
}

int
yfs_client::readdir(inum dir, std::list<dirent> &list)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: you should parse the dirctory content using your defined format,
     * and push the dirents to the list.
     */
	
	std::string buf;
	ec->get(dir, buf);
	if(buf.size()==0){
      return r;
	}
	unsigned int begin = 0, end = 0;
	bool isName = true;
	struct dirent* d;
    while((end = buf.find(',', begin+1))!=std::string::npos){
	  if(isName){
	    d = new struct dirent();
	    d->name = buf.substr(begin+1, end-begin-1);
	  } else {
	    d->inum = n2i(buf.substr(begin+1, end-begin-1));
		list.push_back(*d);
	  }
	  isName = !isName;
	  begin = end;
    }

    if(begin!=buf.size()-1){
      if(!isName){
	    d->inum = n2i(buf.substr(begin+1, buf.size()-begin-1));
		list.push_back(*d);
	  }
    }
	
    return r;
}

int
yfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: read using ec->get().
     */

	std::string buf;
	ec->get(ino, buf);
	if (off >= buf.size()){
	    return r;
	}
    else
	{
		if (buf.size()-off < size){
			data = buf.substr(off, buf.size()-off);
		}
		else {
			data = buf.substr(off, size);
		}
	}

    return r;
}

#define MIN(a,b) (((a) < (b))? (a) : (b))
#define MAX(a,b) (((a) > (b))? (a) : (b))

int
yfs_client::write_(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: write using ec->put().
     * when off > length of original file, fill the holes with '\0'.
     */
	
	std::string buf;
	std::string transfer = data;
	ec->get(ino, buf);
	bytes_written = 0;
	if (off > buf.size()){
		bytes_written += off - buf.size();
		for (off_t i = buf.size(); i < off; i++){
			buf.push_back('\0');
		}
	}
	for (size_t iter = off; iter < MIN(off + size, buf.size()); iter++)
		buf[iter] = data[iter - off];
	for (size_t iter = buf.size(); iter < off + size; iter++)
		buf.push_back(data[iter - off]);
	bytes_written += size;
	ec->put(ino, buf);

    return r;
}

int
yfs_client::write(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
	int r;

	lc->acquire(ino);
	printf("write:lock acquire - inum: %d \n", ino);
	r = write_(ino, size, off, data, bytes_written);
	lc->release(ino);
	printf("write:lock release - inum: %d \n", ino);
    return r;
}

int yfs_client::unlink_(inum parent,const char *name)
{
    int r = OK;

    /*
     * your lab2 code goes here.
     * note: you should remove the file using ec->remove,
     * and update the parent directory content.
     */
	
	std::string buf;
	ec->get(parent, buf);
	if(buf.size()==0){
	  return NOENT;
	}
	std::cout << "buf:" << buf << std::endl;
	unsigned int begin = 0, end = 0;
	unsigned int removeBegin = 0, removeEnd = 0;
	bool isName = true;
	bool found = false;
	struct dirent* d;
    while((end = buf.find(',', begin+1))!=std::string::npos){
	  if(isName){
	    if (buf.substr(begin+1, end-begin-1) == name) {
			removeBegin = begin;
			found = true;
		}
	  } else {
	    if (found) {
			removeEnd = end;
			inum ino = n2i(buf.substr(begin+1, end-begin-1));
			if (isfile(ino)){
				ec->remove(ino);
			}
			found = false;
			break;
		}
	  }
	  isName = !isName;
	  begin = end;
    }
	
    if(begin!=buf.size()-1){
      if(!isName){
	    if (found) {
			removeEnd = buf.size();
			inum ino = n2i(buf.substr(begin+1, buf.size()-begin-1));
			if (isfile(ino)){
				ec->remove(ino);
			}
		}
	  }
    }
	
	buf.replace(removeBegin, removeEnd - removeBegin, "");
	ec->put(parent, buf);

    return r;
}

int yfs_client::unlink(inum parent,const char *name)
{
	int r;

	lc->acquire(parent);
	printf("unlink:lock acquire - inum: %d \n", parent);
	r = unlink_(parent, name);
	lc->release(parent);
	printf("unlink:lock release - inum: %d \n", parent);
    return r;
}
