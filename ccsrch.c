/*
 * ccsrch (c) 2012-2016 Adam Caudill <adam@adamcaudill.com>
 *        (c) 2007 Mike Beekey <zaphod2718@yahoo.com>
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>

#ifndef SIGHUP
  #define SIGHUP 1
#endif
#ifndef SIGQUIT
  #define SIGQUIT 3
#endif

#define PROG_VER "ccsrch 1.0.9 (c) 2012-2016 Adam Caudill <adam@adamcaudill.com>\n             (c) 2007 Mike Beekey <zaphod2718@yahoo.com>"

#define MDBUFSIZE    512
#define MAXPATH     2048
#define BSIZE       4096
#define CARDTYPELEN   64
#define CARDSIZE      17

static char   ccsrch_buf[BSIZE];
static char   lastfilename[MAXPATH];
static char  *exclude_extensions;
static char  *logfilename          = NULL;
static const char  *currfilename   = NULL;
static char  *ignore               = NULL;
static FILE  *logfilefd            = NULL;
static long   total_count          = 0;
static long   file_count           = 0;
static long   currfile_atime       = 0;
static long   currfile_mtime       = 0;
static long   currfile_ctime       = 0;
static time_t init_time            = 0;
static int    cardbuf[CARDSIZE];
static int    print_byte_offset    = 0;
static int    print_epoch_time     = 0;
static int    print_julian_time    = 0;
static int    print_filename_only  = 0;
static int    print_file_hit_count = 0;
static int    ccsrch_index         = 0;
static int    tracksrch            = 0;
static int    tracktype1           = 0;
static int    tracktype2           = 0;
static int    trackdatacount       = 0;
static int    file_hit_count       = 0;
static int    limit_file_results   = 0;
static int    newstatus            = 0;
static int    status_lastupdate    = 0;
static int    status_msglength     = 0;
static int    mask_card_number     = 0;
static int    limit_ascii          = 0;

static void initialize_buffer()
{
  memset(cardbuf, 0, CARDSIZE);
}

static void mask_pan(char *s)
{
  /* Make the PAN number; probably a better way to do this */
  int j;

  for (j=0; s[j]!='\0'; j++) {
    if (j > 3 && j < strlen(s) - 6)
      s[j] = '*';
  }
}

static int track1_srch(int cardlen)
{
  /* [%:B:cardnum:^:name (first initial cap?, let's ignore the %)] */
  if ((ccsrch_buf[ccsrch_index+1] == '^')
      && (ccsrch_buf[ccsrch_index-cardlen] == 'B')
      && (ccsrch_buf[ccsrch_index+2] > '@')
      && (ccsrch_buf[ccsrch_index+2] < '[')) {
    trackdatacount++;
    return 1;
  } else {
    return 0;
  }
}

static int track2_srch(int cardlen)
{
  /* [;:cardnum:=:expir date(YYMM), we'll use the ; here] */
  if (((ccsrch_buf[ccsrch_index+1] == '=') || (ccsrch_buf[ccsrch_index+1] == 'D'))
      && ((ccsrch_buf[ccsrch_index-cardlen] == ';')||
      ((ccsrch_buf[ccsrch_index-cardlen] > '9') || (ccsrch_buf[ccsrch_index-cardlen] < '[')) )
      && ((ccsrch_buf[ccsrch_index+2] > '/')
      && (ccsrch_buf[ccsrch_index+2] < ':'))
      && ((ccsrch_buf[ccsrch_index+3] > '/')
      && (ccsrch_buf[ccsrch_index+3] < ':'))) {
    trackdatacount++;
    return 1;
  }
  else {
    return 0;
  }
}

static void print_result(const char *cardname, int cardlen, long byte_offset)
{
  int		i = 0;
  char	nbuf[20];
  char	buf[MAXPATH];
  char	basebuf[MDBUFSIZE];
  char	bytebuf[MDBUFSIZE];
  char	datebuf[MDBUFSIZE];
  char	mdatebuf[CARDTYPELEN];
  char	adatebuf[CARDTYPELEN];
  char	cdatebuf[CARDTYPELEN];
  char	trackbuf[MDBUFSIZE];

  memset(&nbuf, '\0', sizeof(nbuf));

  for (i=0; i<cardlen; i++)
    nbuf[i] = cardbuf[i]+48;

  if (ignore && strstr(ignore, nbuf) != NULL)
    return;

  memset(&buf,'\0',MAXPATH);
  memset(&basebuf,'\0',MDBUFSIZE);
  
  /* Mask the card if specified */
  if (mask_card_number)
    mask_pan(nbuf);

  /* MB we need to figure out how to update the count and spit out the final
  filename with the count.  ensure that it gets flushed out on the last match
  if you are doing a diff between previous filename and new filename */

  if (print_filename_only) {
    snprintf(basebuf, MDBUFSIZE, "%s", currfilename);
  } else {
    snprintf(basebuf, MDBUFSIZE, "%s\t%s\t%s", currfilename, cardname, nbuf);
  }

  snprintf(buf+strlen(buf), MAXPATH-strlen(buf), "%s", basebuf);

  if (print_byte_offset) {
    memset(&bytebuf,'\0',MDBUFSIZE);
    snprintf(bytebuf, MDBUFSIZE, "\t%ld", byte_offset);
    snprintf(buf+strlen(buf), MAXPATH-strlen(buf), "%s", bytebuf);
  }
  if (print_julian_time) {
    snprintf(mdatebuf, CARDTYPELEN, "%s", ctime((time_t *)&currfile_mtime));
    mdatebuf[strlen(mdatebuf)-1] = '\0';
    snprintf(adatebuf, CARDTYPELEN, "%s", ctime((time_t *)&currfile_atime));
    adatebuf[strlen(mdatebuf)-1] = '\0';
    snprintf(cdatebuf, CARDTYPELEN, "%s", ctime((time_t *)&currfile_atime));
    cdatebuf[strlen(mdatebuf)-1] = '\0';
    snprintf(datebuf, MDBUFSIZE, "\t%s\t%s\t%s", mdatebuf,adatebuf,cdatebuf);
    snprintf(buf+strlen(buf), MAXPATH-strlen(buf), "%s", datebuf);
  }

  if (print_epoch_time) {
    memset(&datebuf,'\0',MDBUFSIZE);
    snprintf(datebuf, MDBUFSIZE, "\t%ld\t%ld\t%ld", currfile_mtime,currfile_atime,currfile_ctime);
    snprintf(buf+strlen(buf), MAXPATH-strlen(buf), "%s", datebuf);
  }

  if (tracksrch) {
    memset(&trackbuf,'\0',MDBUFSIZE);
    if (tracktype1) {
      if (track1_srch(cardlen)) {
        snprintf(trackbuf, MDBUFSIZE, "\tTRACK_1");
      }
    }
    if (tracktype2) {
      if (track2_srch(cardlen)) {
        snprintf(trackbuf, MDBUFSIZE, "\tTRACK_2");
      }
    }
    snprintf(buf+strlen(buf), MAXPATH-strlen(buf), "%s", trackbuf);
  }

  if (logfilefd != NULL) {
    fprintf(logfilefd, "%s\n", buf);
  } else {
    printf("%s\n", buf);
  }

  total_count++;
  file_hit_count++;
}

static void check_mastercard_16(long offset)
{
  char num2buf[7];
  int  vnum = 0;

  memset(&num2buf, 0, sizeof(num2buf));
  snprintf(num2buf, 3, "%d%d", cardbuf[0], cardbuf[1]);
  vnum = atoi(num2buf);
  if ((vnum > 50) && (vnum < 56))
    print_result("MASTERCARD", 16, offset);

  snprintf(num2buf, sizeof(num2buf), "%d%d%d%d%d%d", cardbuf[0], cardbuf[1], cardbuf[2], cardbuf[3], cardbuf[4], cardbuf[5]);
  vnum = atoi(num2buf);
  if ((vnum >= 222100) && (vnum <= 272099))
    print_result("MASTERCARD", 16, offset);
}

static void check_visa_16(long offset)
{
  char  num2buf[2];
  int   vnum = 0;

  memset(&num2buf, 0, sizeof(num2buf));
  snprintf(num2buf, 2, "%d", cardbuf[0]);
  vnum = atoi(num2buf);
  if (vnum == 4)
    print_result("VISA", 16, offset);
}

static void check_discover_16(long offset)
{
  char  num2buf[5];
  int   vnum = 0;

  memset(&num2buf, 0, sizeof(num2buf));
  snprintf(num2buf, 5, "%d%d%d%d", cardbuf[0], cardbuf[1], cardbuf[2], cardbuf[3]);
  vnum = atoi(num2buf);
  if (vnum == 6011)
    print_result("DISCOVER", 16, offset);
}

static void check_jcb_16(long offset)
{
  char  num2buf[5];
  int   vnum = 0;

  memset(&num2buf, 0, sizeof(num2buf));
  snprintf(num2buf, 5, "%d%d%d%d", cardbuf[0], cardbuf[1], cardbuf[2], cardbuf[3]);
  vnum = atoi(num2buf);
  if ((vnum >= 3528) && (vnum <= 3589))
    print_result("JCB", 16, offset);
}

static void check_amex_15(long offset)
{
  char  num2buf[3];
  int   vnum = 0;

  memset(&num2buf, 0, sizeof(num2buf));
  snprintf(num2buf, 3, "%d%d", cardbuf[0], cardbuf[1]);
  vnum = atoi(num2buf);
  if ((vnum == 34) || (vnum == 37))
    print_result("AMEX", 15, offset);
}

static void check_enroute_15(long offset)
{
  char  num2buf[5];
  int   vnum = 0;

  memset(&num2buf, 0, sizeof(num2buf));
  snprintf(num2buf, 5, "%d%d%d%d", cardbuf[0], cardbuf[1], cardbuf[2], cardbuf[3]);
  vnum = atoi(num2buf);
  if ((vnum == 2014) || (vnum == 2149))
    print_result("ENROUTE", 15, offset);
}

static void check_jcb_15(long offset)
{
  char  num2buf[5];
  int   vnum = 0;

  memset(&num2buf, 0, sizeof(num2buf));
  snprintf(num2buf, 5, "%d%d%d%d", cardbuf[0], cardbuf[1], cardbuf[2], cardbuf[3]);
  vnum = atoi(num2buf);
  if ((vnum == 2131) || (vnum == 1800) || (vnum == 3528) || (vnum == 3529))
    print_result("JCB", 15, offset);
}

static void check_diners_club_cb_14(long offset)
{
  char  num2buf[4];
  char  num2buf2[3];
  int   vnum = 0;
  int   vnum2 = 0;

  memset(&num2buf, 0, sizeof(num2buf));
  memset(&num2buf2, 0, sizeof(num2buf2));
  snprintf(num2buf, 4, "%d%d%d", cardbuf[0], cardbuf[1], cardbuf[2]);
  snprintf(num2buf2, 3, "%d%d", cardbuf[0], cardbuf[1]);
  vnum = atoi(num2buf);
  vnum2 = atoi(num2buf2);
  if (((vnum > 299) && (vnum < 306)) || ((vnum > 379) && (vnum < 389)) || (vnum2 == 36) || 
      ((vnum2 >= 38) && (vnum2 <=39)))
    print_result("DINERS_CLUB_CARTE_BLANCHE", 14, offset);
}

static int process_prefix(int len, long offset)
{
  switch (len) {
    case 16:
      check_mastercard_16(offset);
      check_visa_16(offset);
      check_discover_16(offset);
      check_jcb_16(offset);
      break;
    case 15:
      check_amex_15(offset);
      check_enroute_15(offset);
      check_jcb_15(offset);
      break;
    case 14:
      check_diners_club_cb_14(offset);
      break;
  }
  return 0;
}
static int luhn_check(int len, long offset)
{
  int i      = 0;
  int tmp    = 0;
  int total  = 0;
  int nummod = 0;
  int num[CARDSIZE];

  if (cardbuf[i]<=0)
    return 0;

  memset(num, 0, CARDSIZE);

  for (i=0; i<len; i++)
   num[i]=cardbuf[i];

  for (i=len-2; i>=0; i-=2) {
    tmp=2*num[i];
    num[i]=tmp;
    if (num[i]>9)
      num[i]-=9;
  }

  for (i=0; i<len; i++)
    total += num[i];

  nummod = total % 10;
  if (nummod == 0)
  {
#ifdef DEBUG
  printf("Luhn Check passed ***********************************\n");
#endif
    process_prefix(len, offset);
  }
  return nummod;
}

static int is_ascii_buf(const char *buf, int len)
{
  int i;
  for (i=0; i < len; i++) {
    if (!isascii(buf[i]))
      return 0;
  }
  return 1;
}

static char *stolower(char *buf)
{
  char *ptr = buf;

  if (buf == NULL || strlen(buf) == 0)
    return buf;

  while ((*ptr = tolower(*ptr))) ptr++;
  return buf;
}

static void update_status(const char *filename, int position)
{
  struct tm *current;
  time_t     now;
  char       msgbuffer[MDBUFSIZE];
  char      *fn;

  /* if ((int)time(NULL) > status_lastupdate) */
  if (position % (1024 * 1024) == 0 || (int)time(NULL) > status_lastupdate) {
    printf("%*s\r", status_msglength, " ");

    time(&now);
    current = localtime(&now);

    fn = strrchr(filename, '/');

    if (fn == NULL) {
      fn = (char *)filename;
    } else {
      fn++;
    }

    status_msglength = snprintf(msgbuffer, MDBUFSIZE, "[%02i:%02i:%02i File: %s - Processed: %iMB]\r",
      current->tm_hour, current->tm_min, current->tm_sec,
      fn,
      (position / 1024) / 1024);

    printf("%s", msgbuffer);

    fflush(stdout);

    status_lastupdate = time(NULL);
  }
}

static int ccsrch(const char *filename)
{
  FILE  *in            = NULL;
  int   cnt            = 0;
  long  byte_offset    = 1;
  int   k              = 0;
  int   counter        = 0;
  int   total          = 0;
  int   check          = 0;
  int   limit_exceeded = 0;

#ifdef DEBUG
  printf("Processing file %s\n",filename);
#endif

  memset(&lastfilename,'\0',MAXPATH);
  ccsrch_index = 0;
  errno        = 0;
  in = fopen(filename, "rb");
  if (in == NULL) {
    if (errno==13) {
      fprintf(stderr, "ccsrch: Unable to open file %s for reading; Permission Denied\n", filename);
    } else {
      fprintf(stderr, "ccsrch: Unable to open file %s for reading; errno=%d\n", filename, errno);
    }
    return -1;
  }
  currfilename = filename;
  byte_offset=1;

  file_count++;

  initialize_buffer();

  while (1 && limit_exceeded == 0) {
    memset(&ccsrch_buf, '\0', BSIZE);
    cnt = fread(&ccsrch_buf, 1, BSIZE - 1, in);
    if (cnt <= 0)
      break;

    if (limit_ascii && !is_ascii_buf(ccsrch_buf, cnt))
      break;

    for (ccsrch_index=0; ccsrch_index<cnt && limit_exceeded==0; ccsrch_index++) {
      /* check to see if our data is 0...9 (based on ACSII value) */
      if ((ccsrch_buf[ccsrch_index] >= '0') && (ccsrch_buf[ccsrch_index] <= '9')) {
        check = 1;
        cardbuf[counter] = ((int)ccsrch_buf[ccsrch_index])-'0';
        counter++;
      } else if ((ccsrch_buf[ccsrch_index] == 0) || (ccsrch_buf[ccsrch_index] == '\r') ||
      	   (ccsrch_buf[ccsrch_index] == '\n') || (ccsrch_buf[ccsrch_index] == '-')) {
        /*
         * we consider dashes, nulls, new lines, and carriage
         * returns to be noise, so ingore those
         */
        check = 0;
      } else {
        check = 0;
        initialize_buffer();
        counter=0;
      }

      if (((counter > 12) && (counter < CARDSIZE)) && (check)) {
        luhn_check(counter, byte_offset-counter);
      } else if ((counter == CARDSIZE) && (check)) {
        for (k=0; k<counter-1; k++) {
          cardbuf[k] = cardbuf[k + 1];
        }
        cardbuf[k] = (-1);
        luhn_check(13,byte_offset-13);
        luhn_check(14,byte_offset-14);
        luhn_check(15,byte_offset-15);
        luhn_check(16,byte_offset-16);
        counter--;
      }
      byte_offset++;

      if (newstatus == 1)
      	update_status(currfilename, byte_offset);

      /* check to see if we've hit the limit for the current file */
      if (limit_file_results > 0 && file_hit_count >= limit_file_results)
    	  limit_exceeded = 1;
    }
  }

  fclose(in);

  return total;
}

static int escape_space(const char *infile, char *outfile)
{
  int    i       = 0;
  int    spc     = 0;
  char   *tmpbuf = NULL;
  int    filelen = 0;
  int    newlen  = 0;
  int    newpos  = 0;

  filelen = strlen(infile);
  for (i=0; i<filelen; i++) {
    if (infile[i] == ' ')
      spc++;
  }

  newlen = filelen + spc + 1;
  tmpbuf = (char *)malloc(newlen);
  if (tmpbuf == NULL) {
    fprintf(stderr, "escape_space: can't allocate memory; errno=%d\n", errno);
    return 1;
  }
  memset(tmpbuf, '\0', newlen);

  for (i=0; i<filelen; i++) {
    if (infile[i] == ' ') {
      tmpbuf[newpos++] = '\\';
      tmpbuf[newpos] = infile[i];
    } else {
      tmpbuf[newpos] = infile[i];
    }
    newpos++;
  }
  snprintf(outfile, newlen, "%s", tmpbuf);
  free(tmpbuf);
  return 0;
}

static int get_file_stat(const char *inputfile, struct stat *fileattr)
{
  int   err     = 0;
  char *tmp2buf = NULL;

  tmp2buf = strdup(inputfile);
  if (tmp2buf == NULL) {
    fprintf(stderr, "get_file_stat: can't allocate memory; errno=%d\n", errno);
    return 1;
  }

  err = stat(tmp2buf, fileattr);
  if (err != 0) {
    if (errno == ENOENT) {
      fprintf(stderr, "get_file_stat: File %s not found, can't get stat info\n", inputfile);
    } else {
      fprintf(stderr, "get_file_stat: Cannot stat file %s; errno=%d\n", inputfile, errno);
    }
    free(tmp2buf);
    return -1;
  }
  currfile_atime=fileattr->st_atime;
  currfile_mtime=fileattr->st_mtime;
  currfile_ctime=fileattr->st_ctime;
  free(tmp2buf);
  return 0;
}

static char *get_filename_ext(const char *filename)
{
  char *slash = strrchr(filename, '/');
  char *dot   = strrchr(slash, '.');
  if(!dot || dot == slash)
    return "";
  return dot;
}

static int is_allowed_file_type(const char *name)
{
  char  delim[] = ",";
  char *exclude = NULL;
  char *fname   = NULL;
  char *result  = NULL;
  char *ext     = NULL;
  int   ret     = 0;

  if (exclude_extensions == NULL)
    return 0;

  exclude = strdup(exclude_extensions);
  fname   = strdup(name);
  if (exclude == NULL || fname == NULL)
    return 0;

  ext     = get_filename_ext(fname);
  stolower(ext);
  if (ext != NULL && ext[0] != '\0') {
    result = strtok(exclude, delim);
    while (result != NULL) {
      if (strcmp(result, ext) == 0) {
        ret = 1;
        break;
      } else {
        result = strtok(NULL, delim);
      }
    }
  }
  free(exclude);
  free(fname);
  return ret;
}

static int proc_dir_list(const char *instr)
{
  DIR            *dirptr;
  struct dirent  *direntptr;
  int             dir_name_len = 0;
  char           *curr_path    = NULL;
  struct stat     fstat;
  int             err          = 0;
  char            tmpbuf[BSIZE];

  if (instr == NULL)
    return 1;

  dir_name_len = strlen(instr);
  dirptr       = opendir(instr);

#ifdef DEBUG
  printf("Checking directory <%s>\n",instr);
#endif

  if (dirptr == NULL) {
    fprintf(stderr, "proc_dir_list: Can't open dir %s; errno=%d\n", instr, errno);
    return 1;
  }
  curr_path = (char *)malloc(MAXPATH + 1);
  if (curr_path == NULL) {
    fprintf(stderr, "proc_dir_list: Can't allocate enough space; errno=%d\n", errno);
    closedir(dirptr);
    return 1;
  }
  snprintf(curr_path, MAXPATH, "%s", instr);

  while ((direntptr = readdir(dirptr)) != NULL) {
    /* readdir give us everything and not necessarily in order. This
       logic is just silly, but it works */
    if ((strcmp(direntptr->d_name, ".") == 0) ||
        (strcmp(direntptr->d_name, "..") == 0))
      continue;

    snprintf(curr_path+strlen(curr_path), MAXPATH-strlen(curr_path), "%s", direntptr->d_name);
    err = get_file_stat(curr_path, &fstat);

    if (err == -1) {
      if (errno == ENOENT) {
        fprintf(stderr, "proc_dir_list: file %s not found, can't stat\n", curr_path);
      } else {
        fprintf(stderr, "proc_dir_list: Cannot stat file %s; errno=%d\n", curr_path, errno);
      }
      closedir(dirptr);
      free(curr_path);
      return 1;
    }
    if ((fstat.st_mode & S_IFMT) == S_IFDIR) {
      snprintf(curr_path+strlen(curr_path), MAXPATH-strlen(curr_path), "/");
      proc_dir_list(curr_path);
    } else if ((fstat.st_size > 0) && ((fstat.st_mode & S_IFMT) == S_IFREG)) {
      memset(&tmpbuf, '\0', BSIZE);
      if (escape_space(curr_path, tmpbuf) == 0) {
        /* rest file_hit_count so we can keep track of many hits each file has */
        file_hit_count = 0;

        if (is_allowed_file_type(curr_path) == 0) {
	        /*
	         * kludge, need to clean this up
	         * later else any string matching in the path returns non NULL
	         */
	        if (logfilename != NULL) {
	          if (strstr(curr_path, logfilename) != NULL) {
	            fprintf(stderr, "We seem to be hitting our log file, so we'll leave this out of the search -> %s\n", curr_path);
            } else {
	            ccsrch(curr_path);
	            if (file_hit_count > 0 && print_file_hit_count == 1)
	              printf("%s: %d hits\n", curr_path, file_hit_count);
	          }
	        } else {
	          ccsrch(curr_path);
	        }
        }
      }
    }
    curr_path[dir_name_len] = '\0';
  }

  free(curr_path);
  closedir(dirptr);
  return 0;
}

static void cleanup_shtuff()
{
  time_t end_time = time(NULL);
  printf("\n\nFiles searched ->\t\t%ld\n", file_count);
  printf("Search time (seconds) ->\t%ld\n", ((int)time(NULL) - init_time));
  printf("Credit card matches->\t\t%ld\n", total_count);
  if (tracksrch)
    printf("Track data pattern matches->\t%d\n\n", trackdatacount);
  printf("\nLocal end time: %s\n\n", asctime(localtime(&end_time)));
  if (ignore)
    free(ignore);
  if (logfilefd != NULL)
    fclose(logfilefd);
  exit(0);
}

static void signal_proc()
{
  signal(SIGHUP,  cleanup_shtuff);
  signal(SIGTERM, cleanup_shtuff);
  signal(SIGINT,  cleanup_shtuff);
  signal(SIGQUIT, cleanup_shtuff);
}

static void usage(const char *progname)
{
  printf("%s\n", PROG_VER);
  printf("Usage: %s <options> <start path>\n", progname);
  printf("  where <options> are:\n");
  printf("    -a\t\t   Limit to ascii files.\n");
  printf("    -b\t\t   Add the byte offset into the file of the number\n");
  printf("    -e\t\t   Include the Modify Access and Create times in terms \n\t\t   of seconds since the epoch\n");
  printf("    -f\t\t   Only print the filename w/ potential PAN data\n");
  printf("    -i <filename>  Ignore credit card numbers in this list (test cards)\n");
  printf("    -j\t\t   Include the Modify Access and Create times in terms \n\t\t   of normal date/time\n");
  printf("    -o <filename>  Output the data to the file <filename> vs. standard out\n");
  printf("    -t <1 or 2>\t   Check if the pattern follows either a Track 1 \n\t\t   or 2 format\n");
  printf("    -T\t\t   Check for both Track 1 and Track 2 patterns\n");
  printf("    -c\t\t   Show a count of hits per file (only when using -o)\n");
  printf("    -s\t\t   Show live status information (only when using -o)\n");
  printf("    -l N\t   Limits the number of results from a single file before going\n\t\t   on to the next file.\n");
  printf("    -n <list>      File extensions to exclude (i.e .dll,.exe)\n");
  printf("    -m\t\t   Mask the PAN number.\n");
  printf("    -h\t\t   Usage information\n\n");
  printf("See https://github.com/adamcaudill/ccsrch for more information.\n\n");
  exit(0);
}

static int open_logfile()
{
  if (logfilename != NULL) {
    logfilefd = fopen(logfilename, "a+");
    if (logfilefd == NULL) {
      fprintf(stderr, "Unable to open logfile %s for writing; errno=%d\n", logfilename, errno);
      return -1;
    }
  }
  return 0;
}

static int check_dir(const char *name)
{
  DIR *dirptr;

  dirptr = opendir(name);
  if (dirptr!=NULL) {
    closedir(dirptr);
    return 1;
  } else {
    return 0;
  }
}

static char *read_ignore_list(const char *filename, size_t *len)
{
  char  *buf;
  FILE  *infile;

  infile = fopen(filename, "r");
  if (!infile)
    return NULL;

  fseek(infile, 0, SEEK_END);
  *len = ftell(infile);
  fseek(infile, 0, SEEK_SET);
  buf = malloc(*len+1);
  if (buf == NULL)
    return NULL;

  if (fread(buf, 1, *len, infile) == 0)
    fprintf(stderr, "Error with reading buf from %s\n", filename);
  fclose(infile);
  return buf;
}

static void split_ignore_list(char *buf, size_t len)
{
  int i;
  for (i=0; i<len; i++) {
    if (buf[i] == '\n')
      buf[i] = ' ';
  }
}

int main(int argc, char *argv[])
{
  struct stat	ffstat;
  char       *inputstr      = NULL;
  char       *inbuf         = NULL;
  char       *tracktype_str = NULL;
  char        tmpbuf[BSIZE];
  int         err            = 0;
  int         c              = 0;
  int         limit_arg      = 0;
  size_t      len;

  if (argc < 2)
    usage(argv[0]);

  while ((c = getopt(argc, argv,"abefi:jt:To:cml:n:s")) != -1) {
      switch (c) {
        case 'a':
          limit_ascii = 1;
        break;
        case 'b':
          print_byte_offset=1;
          break;
        case 'e':
          print_epoch_time=1;
          break;
        case 'f':
          print_filename_only=1;
          break;
        case 'i':
          ignore = read_ignore_list(optarg, &len);
          if (ignore)
            split_ignore_list(ignore, len);
          break;
        case 'j':
          print_julian_time=1;
          break;
        case 'o':
          logfilename=optarg;
          break;
        case 't':
          tracksrch=1;
          tracktype_str=optarg;
          if (atoi(tracktype_str)==1)
            tracktype1=1;
          else if (atoi(tracktype_str)==2)
            tracktype2=1;
          else
            usage(argv[0]);
          break;
        case 'T':
          tracksrch=1;
          tracktype1=1;
          tracktype2=1;
          break;
        case 'c':
        	print_file_hit_count=1;
        	break;
        case 'm':
          mask_card_number=1;
          break;
        case 'l':
        	limit_arg = atoi(optarg);
        	if (limit_arg > 0)
        	  limit_file_results = limit_arg;
        	else
        		usage(argv[0]);
        	break;
        case 'n':
        	exclude_extensions = stolower(optarg);
        	break;
        case 's':
        	newstatus = 1;

        	break;
        case 'h':
        default:
          usage(argv[0]);
          break;
    }
  }

  /* do some cleanup to make sure that invalid options don't get combined */
  if (logfilename != NULL) {
    if (newstatus == 1) {
    	print_file_hit_count = 0;
    }
  } else {
  	newstatus = 0;
  	print_file_hit_count = 0;
  }

  inputstr = argv[optind];
  if (inputstr == NULL)
    usage(argv[0]);

  if (open_logfile() < 0)
    exit(-1);

  inbuf = strdup(inputstr);
  if (inbuf == NULL) {
    fprintf(stderr, "strdup: cannot allocate memory erro=%d\n", errno);
    cleanup_shtuff();
  }
  signal_proc();

  init_time = time(NULL);
  printf("\n%s\n", PROG_VER);
  printf("\nLocal start time: %s\n",ctime((time_t *)&init_time));
  if (check_dir(inbuf)) {
#ifdef WINDOWS
    if ((inbuf[strlen(inbuf) - 1]) != '\\')
      inbuf[strlen(inbuf)] = '\\';
#else
    if ((inbuf[strlen(inbuf) - 1]) != '/')
      inbuf[strlen(inbuf)] = '/';
#endif
    proc_dir_list(inbuf);
  } else {
    err = get_file_stat(inbuf, &ffstat);
    if (err == -1) {
      if (errno == ENOENT) {
        fprintf(stderr, "File %s not found, can't stat\n", inbuf);
      } else {
        fprintf(stderr, "Cannot stat file %s; errno=%d\n", inbuf, errno);
      }
      exit(-1);
    }

    if ((ffstat.st_size > 0) && ((ffstat.st_mode & S_IFMT) == S_IFREG)) {
      memset(&tmpbuf, '\0', BSIZE);
      if (escape_space(inbuf, tmpbuf) == 0) {
        if (logfilename != NULL) {
          if (strstr(inbuf, logfilename) != NULL) {
            fprintf(stderr, "main: We seem to be hitting our log file, so we'll leave this out of the search -> %s\n", inbuf);
          } else {
#ifdef DEBUG
            printf("Processing file %s\n",inbuf);
#endif
            ccsrch(inbuf);
          }
        } else {
#ifdef DEBUG
          printf("Processing file %s\n",inbuf);
#endif
          ccsrch(inbuf);
        }
      }
    } else if ((ffstat.st_mode & S_IFMT) == S_IFDIR) {
#ifdef WINDOWS
      if ((inbuf[strlen(inbuf) - 1]) != '\\')
        inbuf[strlen(inbuf)] = '\\';
#else
      if ((inbuf[strlen(inbuf) - 1]) != '/')
        inbuf[strlen(inbuf)] = '/';
#endif
      proc_dir_list(inbuf);
    } else {
      fprintf(stderr, "main: Unknown mode returned-> %x\n", ffstat.st_mode);
    }
  }
  free(inbuf);
  cleanup_shtuff();
  return 0;
}
