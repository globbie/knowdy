#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <string.h>
#include <strings.h>
#include <memory.h>

#include <stdarg.h>
#include <syslog.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>


#include <unistd.h>


#define DEBUG_UTILS_LEVEL_1 0
#define DEBUG_UTILS_LEVEL_2 0
#define DEBUG_UTILS_LEVEL_3 0
#define DEBUG_UTILS_LEVEL_4 0
#define DEBUG_UTILS_LEVEL_TMP 1

#include "knd_config.h"
#include "knd_utils.h"

/* base name to integer value mapping */
int obj_id_base[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,  0, 1, 2, 3, 4, 5, 6, 7, 8, 9,-1,-1,-1,-1,-1,-1,
    -1,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24, 25,26,27,28,29,30,31,32,33,34,35,-1,-1,-1,-1,-1,
    -1,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50, 51,52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
};

const char *obj_id_seq = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

extern void 
knd_log(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    /* as daemon use syslog */
    if (getppid() == 1) {
        openlog("knd", LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL1);
        vsyslog(LOG_NOTICE, fmt, args);
        closelog();
    }
    else {
        vprintf(fmt, args);
        printf("\n");
    }
    va_end(args);
}

extern int 
knd_compare(const char *a, const char *b)
{
    for (size_t i = 0; i < KND_ID_SIZE; i++) {
        if (a[i] > b[i]) return knd_MORE;
        else if (a[i] < b[i]) return knd_LESS;
    }
    
    return knd_EQUALS;
}

extern int 
knd_inc_id(char *id)
{
    size_t i;

    i = KND_ID_SIZE - 1;
    begin:
    
    if (id[i] == '9') { id[i] = 'A'; return knd_OK;}
    if (id[i] == 'Z') { id[i] = 'a'; return knd_OK;}
    if (id[i] == 'z') { id[i--] = '0'; goto begin; }
    id[i]++;

    return knd_OK;
}

extern int 
knd_is_valid_id(const char *id, size_t id_size)
{
    if (id_size > KND_ID_SIZE) return knd_FAIL;
    
    for (size_t i = 0; i < KND_ID_SIZE; i++) {
        if (id[i] >= '0' && id[i] <= '9') continue;
        if (id[i] >= 'A' && id[i] <= 'Z') continue;
        if (id[i] >= 'a' && id[i] <= 'z') continue;
        return knd_FAIL;
    }

    return knd_OK;
}

extern const char *
knd_max_id(const char *a, const char *b)
{
    return (knd_compare(a, b) == knd_MORE) ? a : b;
}

extern const char *
knd_min_id(const char *a, const char *b)
{
    return knd_compare(a, b) == knd_LESS ? a : b;
}



extern unsigned char *
knd_pack_int(unsigned char *buf,
             unsigned int val)
{
    buf[0] = val >> 24;
    buf[1] = val >> 16;
    buf[2] = val >> 8;
    buf[3] = val;

    return buf + 4;
}


extern unsigned long
knd_unpack_int(const unsigned char *buf)
{
    unsigned long result = 0;

    result =  buf[3];
    result |= buf[2] << 8;
    result |= buf[1] << 16;
    result |= buf[0] << 24;

    return result;
}



static int 
knd_mkdir(const char *path, mode_t mode)
{
    struct stat st;
    int err = knd_OK;

    if (stat(path, &st) != 0) {

        /* directory does not exist */
        if (mkdir(path, mode) != 0)
            err = knd_FAIL;
    }
    else if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        err = knd_FAIL;
    }

    return err;
}


/**
 * knd_mkpath - ensure all directories in path exist
 */
extern int 
knd_mkpath(const char *path, mode_t mode, bool has_filename)
{
    char *p;
    char *sep = NULL;
    int  err;
    
    char *path_buf = strdup(path);
    if (!path_buf)
        return knd_NOMEM;
    
    err = knd_OK;
    p = path_buf;

    while (1) {
        sep = strchr(p, '/');
        if (!sep) break;

        /*knd_log("CURR PATH: %s\n", path_buf);
          knd_log("REST: %s\n", p); */
        
        *sep = '\0';
        if (!(*p)) goto next_sep;
        
        err = knd_mkdir(path_buf, mode);
        if (err)
            goto final;
        
    next_sep:
        *sep = '/';
        p = sep + 1;
    }

    /*knd_log("LAST DIR: \"%s\"\n", p); */
    
    /* in case no final dir separator is present at the end */
    if (*p && !has_filename) {
        err = knd_mkdir(path, mode);
        if (err)
            goto final;
    }

 final:
    free(path_buf);

    return err;
}

extern int 
knd_write_file(const char *path, const char *filename, 
               void *buf, size_t buf_size)
{
    char name_buf[KND_TEMP_BUF_SIZE];
    int fd;

    sprintf(name_buf, "%s/%s", path, filename);

    /* write textual content */
    fd = open((const char*)name_buf,  
	      O_WRONLY | O_TRUNC | O_CREAT, 0644);
    if (fd < 0) return knd_IO_FAIL;

    write(fd, buf, buf_size);
    close(fd);

    return knd_OK;
}

extern int 
knd_append_file(const char *filename, 
                void *buf, size_t buf_size)
{
    int fd;

    /* write textual content */
    fd = open(filename,  
	      O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return knd_IO_FAIL;

    write(fd, buf, buf_size);
    close(fd);

    return knd_OK;
}


extern int 
knd_get_conc_prefix(const char *name,
		    size_t name_size,
			char *prefix)
{
    int digit_size = KND_CONC_PREFIX_DIGIT_SIZE;
    size_t i = 0;

    if (!name_size) return knd_FAIL;

    for (i = 0; i <= KND_CONC_PREFIX_SIZE; i += digit_size){
        memcpy(prefix, name, i);
        if (i > name_size) break;
    }

    prefix[i - digit_size] = '\0';

    return knd_OK;
}


extern int 
knd_get_trailer(const char   *rec,
                size_t  rec_size,
                char   *trailer_name,
                size_t *trailer_name_size,
                size_t *num_items,
                char   *dir_rec,
                size_t *dir_rec_size)
{
    char buf[KND_LARGE_BUF_SIZE];
    size_t buf_size = 0;

    char num_buf[KND_SMALL_BUF_SIZE];

    char *c;
    char *b;
    const char *curr;
    char *val;
    long numval = 0;

    size_t curr_size;
    size_t numfield_size = 0;
    bool in_facet = false;
    bool in_refset = false;
    int err;

    buf_size = KND_NUMFIELD_MAX_SIZE;
    if (rec_size < KND_NUMFIELD_MAX_SIZE)
        buf_size = rec_size;

    /* get size of trailer */
    curr = rec + rec_size - buf_size;
    memcpy(buf, curr, buf_size);
    buf[buf_size] = '\0';

    /*knd_log("TRAILER BUF: \"%s\"\n", buf);*/
    
    /* find last closing delimiter */
    for (size_t i = buf_size - 1; i > 0; i--) {
        c = buf + i;
        if (!strncmp(c, GSL_CLOSE_DELIM, GSL_CLOSE_DELIM_SIZE)) {
            in_refset = true;
            goto got_close_delim;
        }
        if (!strncmp(c, GSL_CLOSE_FACET_DELIM, GSL_CLOSE_FACET_DELIM_SIZE)) {
            in_facet = true;
            goto got_close_delim;
        }
    }

    knd_log("   -- invalid IDX trailer.. no closing delimiter found.. :(\n");

    return knd_FAIL;

 got_close_delim:
    numfield_size = buf_size - (c - buf) - 1;

    /*knd_log("   ++ Closing delim found, numfield size: %lu\n",
      (unsigned long)numfield_size); */

    if (!numfield_size) {
        *dir_rec_size = 0;
        return knd_OK;
    }
    
    memcpy(num_buf, (const char*)c + 1, numfield_size);
    num_buf[numfield_size] = '\0';
            
    err = knd_parse_num((const char*)num_buf, &numval);
    if (err) return err;

    /* check limits */
    if (numval < 1 || numval >= KND_LARGE_BUF_SIZE)
        return knd_FAIL;

    /* extract trailer */
    buf_size = (size_t)numval;
    
    curr = rec + rec_size - numval - numfield_size;
    memcpy(buf, curr, buf_size);

    /* delete the closing delim */
    buf[buf_size - 1] = '\0';

    /*knd_log("   IDX TRAILER: %s [%lu]\n",
      buf, (unsigned long)buf_size); */

    if (in_facet) {
        if (strncmp(buf, GSL_OPEN_FACET_DELIM, GSL_OPEN_FACET_DELIM_SIZE)) {
            knd_log("   -- invalid IDX trailer.. Facet doesn't start with an opening delim..\n");
            return knd_FAIL;
        }
        c = buf + GSL_OPEN_FACET_DELIM_SIZE;
    }

    if (in_refset) {
        if (strncmp(buf, GSL_OPEN_DELIM, GSL_OPEN_DELIM_SIZE)) {
            knd_log("   -- invalid IDX trailer.. Refset doesn't start with an opening delim..\n");
            return knd_FAIL;
        }
        c = buf + GSL_OPEN_DELIM_SIZE;
    }
    
    curr_size = buf - c;
    if (!curr_size) return knd_FAIL;
    
    val = strstr(c, GSL_TERM_SEPAR);
    if (!val) return knd_FAIL;

    curr_size = val - c;
    *val = '\0';
    
    /* total num items? */
    b = strstr(c, GSL_TOTAL);
    if (b) {
        numfield_size = b - c;
        *b = '\0';
        b++;
        
        err = knd_parse_num((const char*)b, &numval);
        if (err) return err;

        if (DEBUG_UTILS_LEVEL_3)
            knd_log("  TOTAL items: %lu\n", (unsigned long)numval);
        
        *num_items = (size_t)numval;
    }

    /* TODO: check limits */
    
    strncpy(trailer_name, c, curr_size);
    *trailer_name_size = curr_size;

    if (!(buf_size - curr_size -  GSL_TERM_SEPAR_SIZE)) return knd_FAIL;
    c += (curr_size + GSL_TERM_SEPAR_SIZE);
    
    *dir_rec_size = buf_size - curr_size -  GSL_TERM_SEPAR_SIZE;
    strncpy(dir_rec, c, *dir_rec_size);

    return knd_OK;
}


extern int 
knd_get_elem_suffix(const char *name,
                    char *buf)
{
    char suff = 0;
    const char *c;

    c = name;
    
    while (*c++) {
        if (*c != '_') continue;
        if (!c[1]) return knd_FAIL;
        suff = c[1];
    }

    if (!suff)
        return knd_FAIL;

    buf[0] = suff;
    return knd_OK;
}


extern int 
knd_make_id_path(char *buf,
                 const char *path,
                 const char *id, 
                 const char *filename)
{
    char *curr_buf = buf;
    size_t path_size = 0;
    int i;

    if (path) {
        path_size = sprintf(curr_buf, "%s", path);
        curr_buf += path_size;
    }
    
    /* treat each digit in the id as a folder */
    for (i = 0; i < KND_ID_MATRIX_DEPTH; i++) {
	*curr_buf =  '/';
	curr_buf++;
	*curr_buf = id[i];
	curr_buf++;
    }
    
    *curr_buf = '\0';
        
    if (filename)
	sprintf(curr_buf, "/%s", filename);

    return knd_OK;
}


extern int
knd_read_UTF8_char(const char *rec,
                   size_t rec_size,
                   size_t *val,
                   size_t *len)
{
    size_t num_bytes = 0;
    long numval = 0;
    
    /* single byte ASCII-code */
    if ((unsigned char)*rec < 128) {
        if (DEBUG_UTILS_LEVEL_3)
            knd_log("    == ASCII code: %u\n", 
                    (unsigned char)*rec);
        num_bytes++;
        
        *val = (size_t)*rec;
        *len = num_bytes;
        return knd_OK;
    }
    
    /* 2-byte indicator */
    if ((*rec & 0xE0) == 0xC0) {
        if (rec_size < 2) {
            if (DEBUG_UTILS_LEVEL_TMP) 
                knd_log("    -- No payload byte left :(\n");
            return knd_LIMIT;
        }

        if ((rec[1] & 0xC0) != 0x80) {
            if (DEBUG_UTILS_LEVEL_4) 
                knd_log("    -- Invalid UTF-8 payload byte: %2.2x\n", 
                        rec[1]);
            return knd_FAIL;
        }
	    
        numval = ((rec[0] & 0x1F) << 6) | 
            (rec[1] & 0x3F);

        if (DEBUG_UTILS_LEVEL_3)
            knd_log("    == UTF-8 2-byte code: %lu\n",
                    (unsigned long)numval);

        *val = (size_t)numval;
        *len = 2;
        return knd_OK;
    }

    /* 3-byte indicator */
    if ((*rec & 0xF0) == 0xE0) {
        if (rec_size < 3) {
            if (DEBUG_UTILS_LEVEL_3) 
                knd_log("    -- Not enough payload bytes left :(\n");
            return knd_LIMIT;
        }
            
        if ((rec[1] & 0xC0) != 0x80) {
            if (DEBUG_UTILS_LEVEL_3) 
                knd_log("    -- Invalid UTF-8 payload byte: %2.2x\n", 
                        rec[1]);
            return knd_FAIL;
        }
            
        if ((rec[2] & 0xC0) != 0x80) {
            if (DEBUG_UTILS_LEVEL_3) 
                knd_log("   -- Invalid UTF-8 payload byte: %2.2x\n", 
                        rec[2]);
            return knd_FAIL;
        }

        numval = ((rec[0] & 0x0F) << 12)  | 
            ((rec[1] & 0x3F) << 6) | 
            (rec[2] & 0x3F);

        if (DEBUG_UTILS_LEVEL_3) 
            knd_log("    == UTF-8 3-byte code: %lu\n", 
                    (unsigned long)numval);

        *val = (size_t)numval;
        *len = 3;

        return knd_OK;
    }

    if (DEBUG_UTILS_LEVEL_3) 
        knd_log("    -- Invalid UTF-8 code: %2.2x\n",
                *rec);
    return knd_FAIL;
}


/**
 * read XML attr value, allocate memory and copy 
 */
extern int
knd_copy_xmlattr(xmlNode    *input_node,
		const char *attr_name,
		char       **result,
		size_t     *result_size)
{
     char *value;
     char *val_copy;

     value = (char*)xmlGetProp(input_node,  (const xmlChar *)attr_name);
     if (!value) return knd_FAIL;
     
     /* overwrite the previous value if any */
     if ((*result))
	 free((*result));

     (*result_size) = strlen(value);
     val_copy = malloc((*result_size) + 1);
     if (!val_copy) {
	 xmlFree(value);
	 return knd_NOMEM;
     }

     strcpy(val_copy, value);
     xmlFree(value);

     /*printf("ATTR VALUE: %s\n\n", val_copy); */

     (*result) = val_copy;

     return knd_OK;
}

/**
 * read XML attr value into existing buffer
 */
extern int
knd_get_xmlattr(xmlNode    *input_node,
		const char *attr_name,
		char       *result,
		size_t     *result_size)
{
     char *value;
     size_t attr_size;

     value = (char*)xmlGetProp(input_node,  (const xmlChar *)attr_name);
     if (!value) return knd_FAIL;
     
     attr_size = strlen(value);

     /* check output overflow + extra \0 */
     if (*result_size < attr_size + 1)
         return knd_NOMEM;

     memcpy(result, value, attr_size);
     result[attr_size] = '\0';
     *result_size = attr_size;

     xmlFree(value);

     return knd_OK;
}

/**
 * read XML attr numeric value
 */
extern int
knd_get_xmlattr_num(xmlNode *input_node,
		    const char *attr_name,
		    long *result)
{
    char *val;
    long numval;
    char *invalid_num_char = NULL;
    int err = knd_OK;

    val = (char*)xmlGetProp(input_node,  (const xmlChar *)attr_name);
    if (!val) return knd_FAIL;
	    
    errno = 0;
    numval = strtol((const char*)val, &invalid_num_char, KND_NUM_ENCODE_BASE);
    
    /* check for various numeric decoding errors */
    if ((errno == ERANGE && (numval == LONG_MAX || numval == LONG_MIN))
	|| (errno != 0 && numval == 0)) {
	perror("strtol");
	err = knd_FAIL;
	goto final;
    }
    
    if (invalid_num_char == val) {
	fprintf(stderr, "  -- No digits found in \"%s\"\n", 
                val);
	err = knd_FAIL;
        
	goto final;
    }
    
    *result = numval;

final:
    if (val)
	xmlFree(val);
   
    return err;
}




extern int 
knd_parse_matching_braces(const char *rec,
                          size_t *chunk_size)
{
    const char *b;
    const char *c;
    size_t brace_count = 0;
    
    c = rec;
    b = c;
    
    while (*c) {
        switch (*c) {
        case '{':
            brace_count++;
            break;
        case '}':
            if (!brace_count)
                return knd_FAIL;
            brace_count--;
            if (!brace_count) {
                *chunk_size = c - b;
                return knd_OK;
            }
            break;
        default:
            break;
        }
        c++;
    }
    
    return knd_FAIL;
}


/**
 */
extern int
knd_parse_num(const char *val,
	      long *result)
/*int *warning)*/
{
    long numval;
    char *invalid_num_char = NULL;
    int err = knd_OK;

    if (!val) return knd_FAIL;

    errno = 0;

    /* fix common typos, raise a warning */
    /*c = val;
    while (*c) {
        if ((*c) == 'O' || (*c) == 'o') {
            *c = '0';
            warning = KND_IS_TYPO;
        }
        c++;
        }*/
    
    numval = strtol(val, &invalid_num_char, KND_NUM_ENCODE_BASE);
    
    /* check for various numeric decoding errors */
    if ((errno == ERANGE && (numval == LONG_MAX || numval == LONG_MIN)) ||
            (errno != 0 && numval == 0))
    {
        perror("strtol");
        err = knd_FAIL;
        goto final;
    }
    
    if (invalid_num_char == val) {
	fprintf(stderr, "  -- No digits were found in \"%s\"\n", val);
	err = knd_FAIL;
	goto final;
    }
    
    *result = numval;

final:
   
    return err;
}


int
knd_parse_IPV4(char *ip, unsigned long *ip_val)
{
    char *field;
    const char *delim = ".";
    char *last;
    long numval;
    int i = 0, err = 0;
    int bit_shift = 8;
    int curr_shift = 24;
    unsigned long result = 0;

    for (field = strtok_r(ip, delim, &last);
         field;
         field = strtok_r(NULL, delim, &last)) {

        if (i > 3) return -1;

        err = knd_parse_num(field, &numval);
        if (err) return err;

        if (!curr_shift)
            result |= numval;
        else
            result |= numval << curr_shift; 

        i++;
        curr_shift -= bit_shift;
    }

    *ip_val = (unsigned long)result;
    
    return knd_OK;
}


/* copy a sequence of non-whitespace chars */
extern int
knd_read_name(char *output,
              size_t *output_size,
              const char *rec,
              size_t rec_size)
{
    const char *c;
    size_t curr_size = 0;
    size_t i = 0;

    c = rec;
    
    while (*c) {
        switch (*c) {
        case ' ':
            break;
        case '\n':
            break;
        case '\r':
            break;
        case '\t':
            break;
        default:
            *output = *c;
            output++;
            curr_size++;
            break;
        }

        if (i >= rec_size) break;

        i++;
        c++;
    }
   
    *output = '\0';
    *output_size = curr_size;
    
    return knd_OK;
}

/* straitforward extraction of XML attrs */
extern int
knd_get_attr(const char *text,
	     const char *attr,
	     char *value,
	     size_t *val_size)
{
    const char *begin;
    const char *end;
    size_t attr_size = 0;

    begin = strstr(text, attr);
    if (!begin) return knd_FAIL;
    
    begin = index(begin, '\"');
    if (!begin) return knd_FAIL;

    begin++;
    
    end = index(begin, '\"');
    if (!end) return knd_FAIL;

    attr_size = end - begin;
    if (!attr_size) return knd_FAIL;

    /*knd_log("ATTR SIZE: %lu BUF: %lu\n",
            (unsigned long)attr_size,
            (unsigned long)(*val_size)); */
    
    if (attr_size >= (*val_size)) return knd_FAIL;

    memcpy(value, begin, attr_size);

    value[attr_size] = '\0';

    *val_size = attr_size;

    return knd_OK;
}

extern int 
knd_remove_nonprintables(char *data)
{
    unsigned char *c;
    c = (unsigned char*)data;

    while (*c) {
	if (*c < 32) {
	    *c = ' ';
	}
	if (*c == '\"') *c = ' ';
	if (*c == '\'') *c = ' ';
	if (*c == '&') *c = ' ';
	if (*c == '\\') *c = ' '; 
	c++;
    }

    return knd_OK;
}



static void 
knd_signal_handler(int sig)
{
    knd_log("signal caught: %d\n", sig);
    
    switch(sig) {
    case SIGHUP:
        /*log_message(LOG_FILE,"hangup signal catched");*/
        break;
    case SIGTERM:
        /*log_message(LOG_FILE,"terminate signal catched");*/
        exit(0);
        break;
    }
}

extern void 
knd_daemonize(const char *pid_filename)
{
    pid_t pid, sid;
    FILE *pid_file;
    int i, err;

    printf("process id: %d\n", getppid());

    if (getppid() == 1) return; /* already a daemon */

    pid = fork();
    if (pid < 0) exit(1); /* fork error */

    if (pid > 0) exit(0); /* parent exits */

    /* child (daemon) continues */
    sid = setsid(); /* obtain a new process group */

    /* close all descriptors */
    for (i = getdtablesize(); i >= 0; --i) close(i);
    
    /* handle standard I/O */
    i = open("/dev/null", O_RDWR); dup(i); dup(i);

    umask(027); /* set newly created file permissions */
    err = chdir(KND_TMP_DIR); /* change running directory */
    if (err) exit(1);
    
    if (sid < 0)
        exit(1);

    pid_file = fopen(pid_filename, "w");
    fprintf(pid_file, "%d", sid);
    fclose(pid_file);
    
    signal(SIGCHLD, SIG_IGN); /* ignore child */
    signal(SIGTSTP, SIG_IGN); /* ignore tty signals */
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGHUP, knd_signal_handler);  /* catch hangup signal */
    signal(SIGTERM, knd_signal_handler); /* catch kill signal */
}


static int
kndData_del(struct kndData *self)
{
    if (!self) return knd_OK;
    free(self);
    return knd_OK;
}


static int
kndData_reset(struct kndData *self)
{
    if (self->spec) free(self->spec);
    if (self->obj) free(self->obj);
    if (self->body) free(self->body);

    if (self->topics) free(self->topics);
    if (self->index) free(self->index);
    if (self->query) free(self->query);
    if (self->reply) free(self->reply);

    if (self->results) free(self->results);
    if (self->filepath) free(self->filepath);

    if (self->control_msg) free(self->control_msg);

    memset(self, 0, sizeof(struct kndData));

    self->del = kndData_del;
    self->reset = kndData_reset;
  
  return knd_OK;
}

extern int 
kndData_new(struct kndData **data)
{
    struct kndData *self;

    self = malloc(sizeof(struct kndData));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndData));

    self->del = kndData_del;
    self->reset = kndData_reset;

    *data = self;

    return knd_OK;

}
