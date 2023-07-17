#include "cli.h"

void inv_handler(int errno_code, int blockID)
{
    switch (errno_code)
        {       
        case ENODATA:
            derror("The selected block [%d] is not valid.\n", blockID);
            break;
        
        case ENODEV:
            derror("Device not mounted\n");
            break;

        default:
            derror("Error occurred. Try again\n");
            break;
        }
}

void get_handler(int errno_code, int blockID)
{

    switch (errno_code)
        {
        case ENODATA:
            derror("No data is currently valid and associated with the given block ID %d\n", blockID);
            break;
        
        case EINVAL:
            derror("A block was requested whose id [%d] is outside the manageable block limit\n", blockID);
            break;

        case ENODEV:
            derror("Device not mounted\n");
            break;

        default:
            derror("Error occurred. Try again\n");
            break;
        }
}

void put_handler(int errno_code)
{
    switch (errno_code)
        {
        case ENOMEM:
            derror("There is currently no space available on the device for your message\n");
            break;
        
        case EFBIG:
            derror("The device cannot contain messages larger than 4084 bytes\n");
            break;
        
        case ENODEV:
            derror("Device not mounted\n");
            break;

        default:
            derror("Error occurred. Try again\n");
            break;
        }
}