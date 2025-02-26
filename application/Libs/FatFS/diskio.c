/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2019        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#include "ff.h"			/* Obtains integer types */
#include "diskio.h"		/* Declarations of disk functions */
#include "qspi-w25q64.h"
#include "stdio.h"

/* Definitions of physical drive number for each drive */
// #define DEV_RAM		0	/* Example: Map Ramdisk to physical drive 0 */
// #define DEV_MMC		1	/* Example: Map MMC/SD card to physical drive 1 */
// #define DEV_USB		2	/* Example: Map USB MSD to physical drive 2 */
#define EX_FLASH	0	/* 外部 flash drive 0 */


/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

// DSTATUS disk_status (
// 	BYTE pdrv		/* Physical drive nmuber to identify the drive */
// )
// {
// 	DSTATUS stat;
// 	int result;

// 	switch (pdrv) {
// 	case DEV_RAM :
// 		result = RAM_disk_status();

// 		// translate the reslut code here

// 		return stat;

// 	case DEV_MMC :
// 		result = MMC_disk_status();

// 		// translate the reslut code here

// 		return stat;

// 	case DEV_USB :
// 		result = USB_disk_status();

// 		// translate the reslut code here

// 		return stat;
// 	}
// 	return STA_NOINIT;
// }

/* Physical drive nmuber to identify the drive */
DSTATUS disk_status (BYTE pdrv)
{
	return 0;
}




/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

// DSTATUS disk_initialize (
// 	BYTE pdrv				/* Physical drive nmuber to identify the drive */
// )
// {
// 	DSTATUS stat;
// 	int result;

// 	switch (pdrv) {
// 	case DEV_RAM :
// 		result = RAM_disk_initialize();

// 		// translate the reslut code here

// 		return stat;

// 	case DEV_MMC :
// 		result = MMC_disk_initialize();

// 		// translate the reslut code here

// 		return stat;

// 	case DEV_USB :
// 		result = USB_disk_initialize();

// 		// translate the reslut code here

// 		return stat;
// 	}
// 	return STA_NOINIT;
// }

/* Physical drive nmuber to identify the drive */
DSTATUS disk_initialize (BYTE pdrv)
{
	uint8_t res=0;
	switch (pdrv)
	{
		case EX_FLASH:
		{
			if(QSPI_W25Qxx_Init()) {
				res = RES_ERROR;
			}
			break;
		}
		default:
			res = RES_ERROR; 
	}		 
	if(res)return  STA_NOINIT;
	else return 0; //初始化成功
}




/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

// DRESULT disk_read (
// 	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
// 	BYTE *buff,		/* Data buffer to store read data */
// 	LBA_t sector,	/* Start sector in LBA */
// 	UINT count		/* Number of sectors to read */
// )
// {
// 	DRESULT res;
// 	int result;

// 	switch (pdrv) {
// 	case DEV_RAM :
// 		// translate the arguments here

// 		result = RAM_disk_read(buff, sector, count);

// 		// translate the reslut code here

// 		return res;

// 	case DEV_MMC :
// 		// translate the arguments here

// 		result = MMC_disk_read(buff, sector, count);

// 		// translate the reslut code here

// 		return res;

// 	case DEV_USB :
// 		// translate the arguments here

// 		result = USB_disk_read(buff, sector, count);

// 		// translate the reslut code here

// 		return res;
// 	}

// 	return RES_PARERR;
// }

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	DRESULT res;
	switch(pdrv)
	{
		case EX_FLASH:
		{
			for(;count>0;count--)
			{
				if(!QSPI_W25Qxx_ReadBuffer(buff, sector*FF_FLASH_SECTOR_SIZE, FF_FLASH_SECTOR_SIZE)) {
					sector++;
					buff+=FF_FLASH_SECTOR_SIZE;
				} else {
					res = RES_ERROR;
					break;
				}
			}
			res = RES_OK;
			break;
		}
		default:
			res = RES_ERROR;         
	}
	return res;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

// DRESULT disk_write (
// 	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
// 	const BYTE *buff,	/* Data to be written */
// 	LBA_t sector,		/* Start sector in LBA */
// 	UINT count			/* Number of sectors to write */
// )
// {
// 	DRESULT res;
// 	int result;

// 	switch (pdrv) {
// 	case DEV_RAM :
// 		// translate the arguments here

// 		result = RAM_disk_write(buff, sector, count);

// 		// translate the reslut code here

// 		return res;

// 	case DEV_MMC :
// 		// translate the arguments here

// 		result = MMC_disk_write(buff, sector, count);

// 		// translate the reslut code here

// 		return res;

// 	case DEV_USB :
// 		// translate the arguments here

// 		result = USB_disk_write(buff, sector, count);

// 		// translate the reslut code here

// 		return res;
// 	}

// 	return RES_PARERR;
// }

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	DRESULT res = RES_OK;
	switch(pdrv)
	{
		case EX_FLASH://外部flash
			for(;count>0;count--)
			{
				// QSPI flash 特性：先擦除再写入
				if(!QSPI_W25Qxx_SectorErase(sector*FF_FLASH_SECTOR_SIZE) &&	!QSPI_W25Qxx_WriteBuffer((uint8_t*)buff, sector*FF_FLASH_SECTOR_SIZE, FF_FLASH_SECTOR_SIZE)) {
					sector++;
					buff+=FF_FLASH_SECTOR_SIZE;
				} else {
					res = RES_ERROR; 
					break;
				}
			}
			res = RES_OK;
			break;
		default:
			res = RES_ERROR; 
	}
	return res;	
}


#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

// DRESULT disk_ioctl (
// 	BYTE pdrv,		/* Physical drive nmuber (0..) */
// 	BYTE cmd,		/* Control code */
// 	void *buff		/* Buffer to send/receive control data */
// )
// {
// 	DRESULT res;
// 	int result;

// 	switch (pdrv) {
// 	case DEV_RAM :

// 		// Process of the command for the RAM drive

// 		return res;

// 	case DEV_MMC :

// 		// Process of the command for the MMC/SD card

// 		return res;

// 	case DEV_USB :

// 		// Process of the command the USB drive

// 		return res;
// 	}

// 	return RES_PARERR;
// }

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	DRESULT res = RES_OK;
	if(pdrv==EX_FLASH)	//外部FLASH  
		{
			switch(cmd)
			{
				case CTRL_SYNC:
					res = RES_OK; 
					break;	 
				case GET_SECTOR_SIZE:
					*(WORD*)buff = FF_FLASH_SECTOR_SIZE;
					res = RES_OK;
					break;	 
				case GET_BLOCK_SIZE:
					*(WORD*)buff = FF_FLASH_BLOCK_SIZE;
					res = RES_OK;
					break;	 
				case GET_SECTOR_COUNT:
					*(DWORD*)buff = FF_FLASH_SECTOR_COUNT;
					res = RES_OK;
					break;
				default:
					res = RES_PARERR;
					break;
			}
		}else 
			res=RES_ERROR;//其他的不支持
		return res;
}


