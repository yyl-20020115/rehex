/* Reverse Engineer's Hex Editor
 * Copyright (C) 2017 Daniel Collins <solemnwarning@solemnwarning.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#ifdef _WIN32
#include <io.h>
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <list>
#include <stdexcept>
#include <stdio.h>
#include <string>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifdef _WIN32
#define O_NOCTTY 0
#endif

#include "buffer.hpp"
#include "win32lib.hpp"

REHex::Buffer::Block *REHex::Buffer::_block_by_virt_offset(off_t virt_offset)
{
	if(virt_offset >= this->length())
	{
		/* Been asked for an offset beyond the end of the file. */
		return nullptr;
	}
	
	off_t begin = 0, end = blocks.size();
	off_t at = end / 2;
	
	while(1)
	{
		if(blocks[at].virt_offset > virt_offset)
		{
			/* This block begins past the offset we want. */
			end = at;
		}
		else if((blocks[at].virt_offset + blocks[at].virt_length) <= virt_offset)
		{
			/* This block ends before the offset we want. */
			begin = at + 1;
		}
		else{
			/* This block is the one we want. */
			return &(blocks[at]);
		}
		
		assert(begin != end);
		
		/* Reset to the middle of our new search area. */
		at = begin + ((end - begin) / 2);
	}
}

void REHex::Buffer::_load_block(Block *block)
{
	if(block->state != Block::UNLOADED)
	{
		return;
	}
	
	/* TODO: Cycle out not-recently-used CLEAN data buffers if we have a
	 * lot loaded in.
	*/
	
	if(block->virt_length > 0)
	{
		if(fseeko(fh, block->real_offset, SEEK_SET) != 0)
		{
			throw std::runtime_error(std::string("fseeko: ") + strerror(errno));
		}
		
		block->grow(block->virt_length);
		
		if(fread(block->data.data(), block->virt_length, 1, fh) == 0)
		{
			if(feof(fh))
			{
				clearerr(fh);
				throw std::runtime_error("Read error: unexpected end of file");
			}
			else{
				throw std::runtime_error(std::string("Read error: ") + strerror(errno));
			}
		}
	}
	
	block->state = Block::CLEAN;
}

/* Returns true if the given FILE handles refer to the same underlying file.
 * Falls back to comparing the filenames if we cannot identify the actual files.
*/
bool REHex::Buffer::_same_file(FILE *file1, const std::string &name1, FILE *file2, const std::string &name2)
{
	#ifdef _WIN32
	BY_HANDLE_FILE_INFORMATION fi1;
	if(GetFileInformationByHandle((HANDLE)(_get_osfhandle(fileno(file1))), &fi1))
	{
		BY_HANDLE_FILE_INFORMATION fi2;
		if(GetFileInformationByHandle((HANDLE)(_get_osfhandle(fileno(file2))), &fi2))
		{
			return fi1.dwVolumeSerialNumber == fi2.dwVolumeSerialNumber
				&& fi1.nFileIndexHigh == fi2.nFileIndexHigh
				&& fi1.nFileIndexLow == fi2.nFileIndexLow;
		}
		else{
			fprintf(stderr, "Could not GetFileInformationByHandle() open file \"%s\": %s\n",
				name2.c_str(), GetLastError_strerror(GetLastError()).c_str());
		}
	}
	else{
		fprintf(stderr, "Could not GetFileInformationByHandle() open file \"%s\": %s\n",
			name1.c_str(), GetLastError_strerror(GetLastError()).c_str());
	}
	#else
	struct stat st1;
	if(fstat(fileno(file1), &st1) == 0)
	{
		struct stat st2;
		if(fstat(fileno(file2), &st2) == 0)
		{
			return st1.st_dev == st2.st_dev && st1.st_ino == st2.st_ino;
		}
		else{
			fprintf(stderr, "Could not fstat() open file \"%s\": %s\n",
				name2.c_str(), strerror(errno));
		}
	}
	else{
		fprintf(stderr, "Could not fstat() open file \"%s\": %s\n",
			name1.c_str(), strerror(errno));
	}
	#endif
	
	/* TODO: Compare canonicalised paths? */
	return file1 == file2;
}

REHex::Buffer::Buffer():
	fh(nullptr),
	block_size(DEFAULT_BLOCK_SIZE)
{
	blocks.push_back(Block(0,0));
	blocks.back().state = Block::CLEAN;
}

REHex::Buffer::Buffer(const std::string &filename, off_t block_size):
	filename(filename), block_size(block_size)
{
	fh = fopen(filename.c_str(), "rb");
	if(fh == NULL)
	{
		throw std::runtime_error(std::string("Could not open file: ") + strerror(errno));
	}
	
	/* Find out the length of the file. */
	
	if(fseeko(fh, 0, SEEK_END) != 0)
	{
		int err = errno;
		fclose(fh);
		throw std::runtime_error(std::string("fseeko: ") + strerror(err));
	}
	
	off_t file_length = ftello(fh);
	if(file_length == -1)
	{
		int err = errno;
		fclose(fh);
		throw std::runtime_error(std::string("ftello: ") + strerror(err));
	}
	
	/* Populate the blocks list with appropriate offsets and sizes. */
	
	for(off_t offset = 0; offset < file_length; offset += block_size)
	{
		blocks.push_back(Block(offset, std::min((file_length - offset), (off_t)(block_size))));
	}
	
	if(file_length == 0)
	{
		blocks.push_back(Block(0,0));
	}
}

REHex::Buffer::~Buffer()
{
	if(fh != NULL)
	{
		fclose(fh);
		fh = NULL;
	}
}

void REHex::Buffer::write_inplace()
{
	write_inplace(filename);
}

void REHex::Buffer::write_inplace(const std::string &filename)
{
	/* Need to open the file with open() since fopen() can't be told to open
	 * the file, creating it if it doesn't exist, WITHOUT truncating and letting
	 * us write at arbitrary positions.
	*/
	int fd = open(filename.c_str(), (O_RDWR | O_CREAT | O_NOCTTY), 0777);
	if(fd == -1)
	{
		throw std::runtime_error(std::string("Could not open file: ") + strerror(errno));
	}
	
	FILE *wfh = fdopen(fd, "r+b");
	if(wfh == NULL)
	{
		close(fd);
		throw std::runtime_error(std::string("Could not open file: ") + strerror(errno));
	}
	
	/* Disable write buffering */
	setbuf(wfh, NULL);
	
	off_t out_length = this->length();
	
	/* Reserve space in the output file if it isn't already at least as large
	 * as the file we want to write out.
	*/
	
	{
		if(fseeko(wfh, 0, SEEK_END) != 0)
		{
			int err = errno;
			fclose(wfh);
			throw std::runtime_error(std::string("fseeko: ") + strerror(err));
		}
		
		off_t wfh_initial_size = ftello(wfh);
		if(wfh_initial_size == -1)
		{
			int err = errno;
			fclose(wfh);
			throw std::runtime_error(std::string("ftello: ") + strerror(err));
		}
		
		if(wfh_initial_size < out_length)
		{
			/* Windows (or GCC/MinGW) provides an ftruncate(), but for some reason it
			 * fails with "File too large" if you try expanding a file with it.
			*/
			
			#ifdef _WIN32
			if(_chsize_s(fileno(wfh), out_length) != 0)
			#else
			if(ftruncate(fileno(wfh), out_length) == -1)
			#endif
			{
				int err = errno;
				fclose(wfh);
				throw std::runtime_error(std::string("Could not expand file: ") + strerror(err));
			}
		}
	}
	
	/* Are we updating the file we originally read data in from? */
	bool updating_file = (fh != NULL && _same_file(fh, this->filename, wfh, filename));
	
	std::list<Block*> pending;
	for(auto b = blocks.begin(); b != blocks.end(); ++b)
	{
		pending.push_back(&(*b));
	}
	
	for(auto b = pending.begin(); b != pending.end();)
	{
		if(updating_file && ((*b)->virt_offset == (*b)->real_offset && (*b)->state != Block::DIRTY))
		{
			/* We're updating the file we originally read data in from and this block
			 * hasn't changed (in contents or offset), don't need to do anything.
			*/
			b = pending.erase(b);
			continue;
		}
		
		auto next = std::next(b);
		
		if(next != pending.end() && (*b)->virt_offset + (*b)->virt_length > (*next)->real_offset)
		{
			/* Can't flush this block yet; we'd write into the data of the next one.
			 *
			 * In order for this to happen, the set of blocks before the next one must
			 * have grown in length, which means the virt_offset of the next block MUST
			 * be greater than its real_offset and so it won't be written to the file
			 * preceeding it, where it could overwrite data still needed to shuffle
			 * clean blocks to higher offsets.
			*/
			
			++b;
			continue;
		}
		
		if((*b)->virt_length > 0)
		{
			_load_block(*b);
			
			if(fseeko(wfh, (*b)->virt_offset, SEEK_SET) != 0)
			{
				int err = errno;
				fclose(wfh);
				throw std::runtime_error(std::string("fseeko: ") + strerror(err));
			}
			
			if(fwrite((*b)->data.data(), (*b)->virt_length, 1, wfh) == 0)
			{
				if(updating_file)
				{
					/* Ensure the block is marked as dirty, since we may have
					 * partially rewritten it in the underlying file and no
					 * longer be able to correctly reload it.
					*/
					(*b)->state = Block::DIRTY;
				}
				
				int err = errno;
				fclose(wfh);
				throw std::runtime_error(std::string("Write error: ") + strerror(err));
			}
			
			if(updating_file)
			{
				/* We've successfuly updated this block in the underlying file.
				 * Mark it as clean and fix the offsets.
				*/
				
				(*b)->real_offset = (*b)->virt_offset;
				(*b)->state       = Block::CLEAN;
			}
		}
		
		b = pending.erase(b);
		
		if(b != pending.begin())
		{
			/* This isn't the first pending block, so we must've stepped
			 * forwards to make a hole for one or more previous ones.
			 * 
			 * We've made the hole, so start walking backwards and writing
			 * out the new blocks.
			*/
			
			--b;
		}
	}
	
	if(ftruncate(fileno(wfh), out_length) == -1)
	{
		int err = errno;
		fclose(wfh);
		throw std::runtime_error(std::string("Could not truncate file: ") + strerror(err));
	}
	
	if(!updating_file)
	{
		/* We've written out a complete new file, and it is now the backing store for this
		 * Buffer. Rebuild the block list so the offsets are correct.
		*/
		
		blocks.clear();
		
		for(off_t offset = 0; offset < out_length; offset += block_size)
		{
			blocks.push_back(Block(offset, std::min((out_length - offset), block_size)));
		}
		
		if(out_length == 0)
		{
			blocks.push_back(Block(0,0));
		}
	}
	
	if(fh != NULL)
	{
		fclose(fh);
	}
	
	/* The Buffer is now backed by the new file (which might be the old one). */
	
	fh = wfh;
	this->filename = filename;
}

void REHex::Buffer::write_copy(const std::string &filename)
{
	FILE *out = fopen(filename.c_str(), "wb");
	if(out == NULL)
	{
		throw std::runtime_error(std::string("Could not open file: ") + strerror(errno));
	}
	
	/* Disable write buffering */
	setbuf(out, NULL);
	
	for(auto b = blocks.begin(); b != blocks.end(); ++b)
	{
		if(b->virt_length > 0)
		{
			_load_block(&(*b));
			
			if(fwrite(b->data.data(), b->virt_length, 1, out) == 0)
			{
				fclose(out);
				throw std::runtime_error(std::string("Write error: ") + strerror(errno));
			}
		}
	}
	
	fclose(out);
}

off_t REHex::Buffer::length()
{
	return blocks.back().virt_offset + blocks.back().virt_length;
}

std::vector<unsigned char> REHex::Buffer::read_data(off_t offset, off_t max_length)
{
	Block *block = _block_by_virt_offset(offset);
	if(block == nullptr)
	{
		return std::vector<unsigned char>();
	}
	
	std::vector<unsigned char> data;
	
	while(block < blocks.data() + blocks.size() && max_length > 0)
	{
		_load_block(block);
		
		off_t block_rel_off = offset - block->virt_offset;
		off_t block_rel_len = block->virt_length - block_rel_off;
		off_t to_copy = std::min(block_rel_len, max_length);
		
		const unsigned char *base = block->data.data() + block_rel_off;
		data.insert(data.end(), base, base + to_copy);
		
		++block;
		
		offset      = block->virt_offset;
		max_length -= to_copy;
	}
	
	return data;
}

bool REHex::Buffer::overwrite_data(off_t offset, unsigned const char *data, off_t length)
{
	if((offset + length) > this->length())
	{
		/* Runs past the end of the buffer. */
		return false;
	}
	
	Block *block = _block_by_virt_offset(offset);
	assert(block != nullptr);
	
	while(length > 0)
	{
		_load_block(block);
		
		off_t block_rel_off = offset - block->virt_offset;
		off_t to_copy = std::min((block->virt_length - block_rel_off), length);
		
		memcpy((block->data.data() + block_rel_off), data, to_copy);
		
		block->state = Block::DIRTY;
		
		data   += to_copy;
		offset += to_copy;
		length -= to_copy;
		
		++block;
	}
	
	return true;
}

bool REHex::Buffer::insert_data(off_t offset, unsigned const char *data, off_t length)
{
	if(offset > this->length())
	{
		/* Starts past the end of the buffer. */
		return false;
	}
	
	/* Need to special-case the block to be the last one when appending. */
	
	Block *block = (offset == this->length()
		? &(blocks.back())
		: _block_by_virt_offset(offset));
	
	assert(block != nullptr);
	
	_load_block(block);
	
	/* Ensure the block's data buffer is large enough */
	
	block->grow(block->virt_length + length);
	
	/* Insert the new data, shifting the rest of the buffer along if necessary */
	
	off_t block_rel_off = offset - block->virt_offset;
	unsigned char *dst = block->data.data() + block_rel_off;
	
	memmove(dst + length, dst, block->virt_length - block_rel_off);
	memcpy(dst, data, length);
	
	block->virt_length += length;
	block->state = Block::DIRTY;
	
	/* Shift the virtual offset of any subsequent blocks along. */
	
	for(++block; block < blocks.data() + blocks.size(); ++block)
	{
		block->virt_offset += length;
	}
	
	return true;
}

bool REHex::Buffer::erase_data(off_t offset, off_t length)
{
	if((offset + length) > this->length())
	{
		/* Runs past the end of the buffer. */
		return false;
	}
	
	Block *block = _block_by_virt_offset(offset);
	assert(block != nullptr);
	
	for(off_t erased = 0; erased < length;)
	{
		off_t block_rel_off = offset - block->virt_offset;
		off_t to_erase = std::min((block->virt_length - block_rel_off), (length - erased));
		
		if(block_rel_off == 0 && to_erase == block->virt_length)
		{
			block->virt_length = 0;
		}
		else{
			_load_block(block);
			
			unsigned char *base = block->data.data() + block_rel_off;
			memmove(base, base + to_erase, block->virt_length - block_rel_off);
			
			block->virt_length -= to_erase;
		}
		
		block->state = Block::DIRTY;
		
		/* Shift the offset back by however many bytes we've already
		 * erased from previous blocks.
		*/
		block->virt_offset -= erased;
		
		erased += to_erase;
		++block;
		
		/* Set the offset to the start of the next block where we'll
		 * pick up the erasing at.
		*/
		offset = block->virt_offset;
	}
	
	/* Shift the virtual offset of any subsequent blocks back. */
	
	for(; block < blocks.data() + blocks.size(); ++block)
	{
		block->virt_offset -= length;
	}
	
	return true;
}

REHex::Buffer::Block::Block(off_t offset, off_t length):
	real_offset(offset),
	virt_offset(offset),
	virt_length(length),
	state(UNLOADED) {}

void REHex::Buffer::Block::grow(off_t min_size)
{
	if(min_size < data.size())
	{
		/* Don't ever shrink the buffer here. */
		return;
	}
	
	if(min_size == data.size() + 1)
	{
		/* If we've been asked to grow the block by one byte, someone
		 * is probably typing new bytes in insert mode. Grow the buffer
		 * by 64 bytes instead so we don't have to grow it and move the
		 * whole buffer around on each keypress.
		*/
		min_size += 63;
	}
	
	data.resize(min_size);
}
