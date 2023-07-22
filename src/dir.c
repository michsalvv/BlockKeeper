#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "blk_fs.h"

/**********************************************************
 * Dir operation
 **********************************************************/


//this iterate function just returns 3 entries: . and .. and then the name of the unique file of the file system
static int fs_iterate(struct file *file, struct dir_context* ctx) {

        if(ctx->pos >= (2 + 1)) return 0;//we cannot return more than . and .. and the unique file entry

        if (ctx->pos == 0){
                if(!dir_emit(ctx,".", 1, FS_ROOT_INODE_NUMBER, DT_UNKNOWN)){
                        return 0;
                }
                else{
                        ctx->pos++;
                }
        }

        if (ctx->pos == 1){
                //here the inode number does not care
                if(!dir_emit(ctx,"..", 2, 1, DT_UNKNOWN)){
                        return 0;
                }
                else{
                        ctx->pos++;
                }
        }
        
        if (ctx->pos == 2){
                if(!dir_emit(ctx, FILE_NAME, strlen(FILE_NAME), FS_UNIQFILE_INODE_NUMBER, DT_UNKNOWN)){
                        return 0;
                }
                else{
                        ctx->pos++;
                }
        }
        return 0;
}

//add the iterate function in the dir operations
const struct file_operations fs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate = fs_iterate,
};
