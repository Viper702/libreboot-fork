/*
 *  gbe/gbe.h
 *  This file is part of the ich9deblob utility from the libreboot project
 *
 *  Copyright (C) 2014 Steve Shenton <sgsit@libreboot.org>
 *                     Francis Rowe <info@gluglug.org.uk>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
 
/*
 * Purpose: provide struct representing gbe region.
 * Map actual buffers of this regions, directly to instances of these
 * structs. This makes working with gbe really easy.
 */
 
/*
 * bit fields used, corresponding to datasheet. See links to datasheets
 * and documentation in ich9deblob.c
 */

#ifndef GBESTRUCT_H
#define GBESTRUCT_H

#include <stdio.h>
#include <string.h>

/* Size of the full gbe region in bytes */
#define GBEREGIONSIZE_8K 0x2000
/*
 * Size of each sub-region in gbe. 
 * gbe contains two regions which
 * can be identical: main and backup.
 * These are each half the size of the full region
 */
#define GBEREGIONSIZE_4K 0x1000

/*
 * These will have a modified descriptor+gbe based on what's in the factory.rom
 * These will be joined into a single 12KiB buffer (descriptor, then gbe) and saved to a file
 * NOTE: The GBE region of 8K is actually 2x 4K regions in a single region; both 4K blocks can be identical (and by default, are)
 * The 2nd one is a "backup", but we don't know when it's used. perhaps it's used when the checksum on the first one does not match?
 */

/*
 * ---------------------------------------------------------------------
 * Gbe struct representing the data:
 * ---------------------------------------------------------------------
 */ 

struct GBEREGIONRECORD_4K {
	unsigned char macAddress[6]; /* 0x03 words, or 0x06 bytes */
	unsigned char otherStuff[120];  /* 0x3c words, or 0x7E bytes */
	unsigned short checkSum; /* when added to the sum of all words above, this should be 0xBABA */
	unsigned char padding1[3968];
};

/*  main and backup region in gbe */ 
struct GBEREGIONRECORD_8K {
	struct GBEREGIONRECORD_4K main;
	struct GBEREGIONRECORD_4K backup;
	/* 
	 * Backup region:
	 * This is actually "main" on X200, since the real main has a bad checksum
	 * and other errors. You should do what you need on this one (if modifying
	 * lenovobios's gbe region) and then copy to main
	 */
};

/*
 * ---------------------------------------------------------------------
 * Gbe functions:
 * ---------------------------------------------------------------------
 */

/* Read a 16-bit unsigned int from a supplied region buffer */
unsigned short gbeGetRegionWordFrom8kBuffer(int index, char* regionData)
{
	return *((unsigned short*)(regionData + (index * 2)));
}

/* 
 * checksum calculation for 8k gbe region (algorithm based on datasheet)
 * also works for 4k buffers, so long as isBackup remains false
 */
unsigned short gbeGetChecksumFrom8kBuffer(char* regionData, unsigned short desiredValue, char isBackup)
{
	int i;
	
	unsigned short regionWord; /* store words here for adding to checksum */
	unsigned short checksum = 0; /* this gbe's checksum */
	unsigned short offset = 0; /* in bytes, from the start of the gbe region. */
	
	/* 
	 * if isBackup is true, use 2nd gbe region ("backup" region)
	 * this function uses *word* not *byte* indexes, hence the bit shift.
	 */
	if (isBackup) offset = GBEREGIONSIZE_4K>>1;

	for (i = 0; i < 0x3F; i++) {
		regionWord = gbeGetRegionWordFrom8kBuffer(i+offset, regionData);
		checksum += regionWord;
	}
	checksum = desiredValue - checksum;
	return checksum;
}

/* checksum calculation for 4k gbe struct (algorithm based on datasheet) */
unsigned short gbeGetChecksumFrom4kStruct(struct GBEREGIONRECORD_4K gbeStruct4k, unsigned short desiredValue)
{
	char gbeBuffer4k[GBEREGIONSIZE_4K];
	memcpy(&gbeBuffer4k, &gbeStruct4k, GBEREGIONSIZE_4K);
	return gbeGetChecksumFrom8kBuffer(gbeBuffer4k, desiredValue, 0);
}

/* modify the gbe region extracted from a factory.rom dump */
struct GBEREGIONRECORD_8K deblobbedGbeStructFromFactory(struct GBEREGIONRECORD_8K factoryGbeStruct8k) 
{	
	/*
	 * Correct the main gbe region. By default, the X200 (as shipped from Lenovo) comes
	 * with a broken main gbe region, where the backup gbe region is used instead. Modify
	 * it so that the main region is usable.
	 */
	
	struct GBEREGIONRECORD_8K deblobbedGbeStruct8k;
	memcpy(&deblobbedGbeStruct8k, &factoryGbeStruct8k, GBEREGIONSIZE_8K);
	
	deblobbedGbeStruct8k.backup.checkSum = gbeGetChecksumFrom4kStruct(deblobbedGbeStruct8k.backup, 0xBABA);
	memcpy(&deblobbedGbeStruct8k.main, &deblobbedGbeStruct8k.backup, GBEREGIONSIZE_4K);
	
	/*
	 * Debugging:
	 * calculate the 0x3F'th 16-bit uint to make the desired final checksum for GBe
	 * observed checksum matches (from X200 factory.rom dumps) on main: 0x3ABA 0x34BA 0x40BA. spec defined as 0xBABA.
	 * X200 ships with a broken main gbe region by default (invalid checksum, and more)
	 * The "backup" gbe regions on these machines are correct, though, and is what the machines default to
	 * For libreboot's purpose, we can do much better than that by fixing the main one... below is only debugging
	 */
	printf("\nfactory Gbe (main): calculated Gbe checksum: 0x%hx and actual GBe checksum: 0x%hx\n", gbeGetChecksumFrom4kStruct(factoryGbeStruct8k.main, 0xBABA), factoryGbeStruct8k.main.checkSum);
	printf("factory Gbe (backup) calculated Gbe checksum: 0x%hx and actual GBe checksum: 0x%hx\n", gbeGetChecksumFrom4kStruct(factoryGbeStruct8k.backup, 0xBABA), factoryGbeStruct8k.backup.checkSum);
	printf("\ndeblobbed Gbe (main): calculated Gbe checksum: 0x%hx and actual GBe checksum: 0x%hx\n", gbeGetChecksumFrom4kStruct(deblobbedGbeStruct8k.main, 0xBABA), deblobbedGbeStruct8k.main.checkSum);
	printf("deblobbed Gbe (backup) calculated Gbe checksum: 0x%hx and actual GBe checksum: 0x%hx\n", gbeGetChecksumFrom4kStruct(deblobbedGbeStruct8k.backup, 0xBABA), deblobbedGbeStruct8k.backup.checkSum);
	
	return deblobbedGbeStruct8k;
}

#endif
