/*
    $Id: freebsd.c,v 1.15 2004/05/27 12:10:21 rocky Exp $

    Copyright (C) 2003, 2004 Rocky Bernstein <rocky@panix.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/* This file contains FreeBSD-specific code and implements low-level
   control of the CD drive. Culled initially I think from xine's or
   mplayer's FreeBSD code with lots of modifications.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

static const char _rcsid[] = "$Id: freebsd.c,v 1.15 2004/05/27 12:10:21 rocky Exp $";

#include "freebsd.h"

#ifdef HAVE_FREEBSD_CDROM

#include <cdio/sector.h>

static access_mode_t 
str_to_access_mode_freebsd(const char *psz_access_mode) 
{
  const access_mode_t default_access_mode = DEFAULT_FREEBSD_AM;

  if (NULL==psz_access_mode) return default_access_mode;
  
  if (!strcmp(psz_access_mode, "ioctl"))
    return _AM_IOCTL;
  else if (!strcmp(psz_access_mode, "CAM"))
    return _AM_CAM;
  else {
    cdio_warn ("unknown access type: %s. Default IOCTL used.", 
	       psz_access_mode);
    return default_access_mode;
  }
}

static void
_free_freebsd (void *obj)
{
  _img_private_t *env = obj;

  if (NULL == env) return;

  if (NULL != env->device) free(env->device);

  if (_AM_CAM == env->access_mode) 
    return free_freebsd_cam(env);
  else 
    return cdio_generic_free(obj);
}

/* Check a drive to see if it is a CD-ROM 
   Return 1 if a CD-ROM. 0 if it exists but isn't a CD-ROM drive
   and -1 if no device exists .
*/
static bool
cdio_is_cdrom(char *drive, char *mnttype)
{
  return cdio_is_cdrom_freebsd_ioctl(drive, mnttype);
}

/*!
   Reads a single mode2 sector from cd device into data starting from lsn.
   Returns 0 if no error. 
 */
static int
_read_audio_sectors_freebsd (void *env, void *data, lsn_t lsn,
			     unsigned int nblocks)
{
  return read_audio_sectors_freebsd_ioctl(env, data, lsn, nblocks);
}

/*!
   Reads a single mode2 sector from cd device into data starting
   from lsn. Returns 0 if no error. 
 */
static int
_read_mode2_sector_freebsd (void *env, void *data, lsn_t lsn, 
			    bool b_form2)
{
  _img_private_t *_obj = env;
  if ( _obj->access_mode == _AM_CAM )
    return read_mode2_sectors_freebsd_cam(_obj, data, lsn, 1, b_form2);
  else
    return read_mode2_sector_freebsd_ioctl(_obj, data, lsn, b_form2);
}

/*!
   Reads nblocks of mode2 sectors from cd device into data starting
   from lsn.
   Returns 0 if no error. 
 */
static int
_read_mode2_sectors_freebsd (void *env, void *data, lsn_t lsn, 
			  bool b_form2, unsigned int nblocks)
{
  _img_private_t *_obj = env;
  int i;
  int retval;

  if ( _obj->access_mode == _AM_CAM )
    return read_mode2_sectors_freebsd_cam(_obj, data, lsn, nblocks, b_form2);
  
  for (i = 0; i < nblocks; i++) {
    if (b_form2) {
      if ( (retval = read_mode2_sector_freebsd_ioctl (_obj, 
					  ((char *)data) + (M2RAW_SECTOR_SIZE * i),
					  lsn + i, true)) )
	return retval;
    } else {
      char buf[M2RAW_SECTOR_SIZE] = { 0, };
      if ( (retval = read_mode2_sector_freebsd_ioctl (_obj, buf, 
						      lsn + i, true)) )
	return retval;
      
      memcpy (((char *)data) + (CDIO_CD_FRAMESIZE * i), 
	      buf + CDIO_CD_SUBHEADER_SIZE, CDIO_CD_FRAMESIZE);
    }
  }
  return 0;
}

/*!
   Return the size of the CD in logical block address (LBA) units.
 */
static uint32_t 
_stat_size_freebsd (void *obj)
{
  _img_private_t *env = obj;

  if (NULL == env) return CDIO_INVALID_LBA;

  if (_AM_CAM == env->access_mode) 
    return stat_size_freebsd_cam(env);
  else 
    return stat_size_freebsd_ioctl(env);
}

/*!
  Set the key "arg" to "value" in source device.
*/
static int
_set_arg_freebsd (void *env, const char key[], const char value[])
{
  _img_private_t *_obj = env;

  if (!strcmp (key, "source"))
    {
      if (!value)
	return -2;

      free (_obj->gen.source_name);
      
      _obj->gen.source_name = strdup (value);
    }
  else if (!strcmp (key, "access-mode"))
    {
      _obj->access_mode = str_to_access_mode_freebsd(value);
      if (_obj->access_mode == _AM_CAM && !_obj->b_cam_init) 
	return init_freebsd_cam(_obj) ? 1 : -3;
      return 0;
    }
  else 
    return -1;

  return 0;
}

/*! 
  Read and cache the CD's Track Table of Contents and track info.
  Return false if successful or true if an error.
*/
static bool
_cdio_read_toc (_img_private_t *_obj) 
{
  track_t i, j;

  /* read TOC header */
  if ( ioctl(_obj->gen.fd, CDIOREADTOCHEADER, &_obj->tochdr) == -1 ) {
    cdio_error("error in ioctl(CDIOREADTOCHEADER): %s\n", strerror(errno));
    return false;
  }

  j=0;
  for ( i = _obj->tochdr.starting_track; i <= _obj->tochdr.ending_track; 
       i++, j++) {
    _obj->tocent[j].track = i;
    _obj->tocent[j].address_format = CD_LBA_FORMAT;

    if ( ioctl(_obj->gen.fd, CDIOREADTOCENTRY, &(_obj->tocent[j]) ) ) {
      cdio_error("%s %d: %s\n",
		 "error in ioctl CDROMREADTOCENTRY for track", 
		 i, strerror(errno));
      return false;
    }
  }

  _obj->tocent[j].track          = CDIO_CDROM_LEADOUT_TRACK;
  _obj->tocent[j].address_format = CD_LBA_FORMAT;
  if ( ioctl(_obj->gen.fd, CDIOREADTOCENTRY, &(_obj->tocent[j]) ) ){
    cdio_error("%s: %s\n",
	       "error in ioctl CDROMREADTOCENTRY for leadout track", 
	       strerror(errno));
    return false;
  }

  return true;
}

/*!
  Eject media. Return 1 if successful, 0 otherwise.
 */
static int 
_eject_media_freebsd (void *env) 
{
  _img_private_t *_obj = env;

  return (_obj->access_mode == _AM_IOCTL) 
    ? eject_media_freebsd_ioctl(_obj) 
    : eject_media_freebsd_cam(_obj);
}

/*!
  Return the value associated with the key "arg".
*/
static const char *
_get_arg_freebsd (void *env, const char key[])
{
  _img_private_t *_obj = env;

  if (!strcmp (key, "source")) {
    return _obj->gen.source_name;
  } else if (!strcmp (key, "access-mode")) {
    switch (_obj->access_mode) {
    case _AM_IOCTL:
      return "ioctl";
    case _AM_CAM:
      return "CAM";
    case _AM_NONE:
      return "no access method";
    }
  } 
  return NULL;
}

/*!
  Return the number of of the first track. 
  CDIO_INVALID_TRACK is returned on error.
*/
static track_t
_get_first_track_num_freebsd(void *env) 
{
  _img_private_t *_obj = env;
  
  if (!_obj->toc_init) _cdio_read_toc (_obj) ;

  return FIRST_TRACK_NUM;
}

/*!
  Return the media catalog number MCN.

  Note: string is malloc'd so caller should free() then returned
  string when done with it.

  FIXME: This is just a guess. 

 */
static char *
_get_mcn_freebsd (const void *env) {

#if FIXED
  const _img_private_t *_obj = env;
  struct ioc_read_subchannel subchannel;
  struct cd_sub_channel_info subchannel_info;

  subchannel.address_format = CD_LBA_FORMAT;
  subchannel.data_format    = CD_MEDIA_CATALOG;
  subchannel.track          = 0;
  subchannel.data_len       = 1;
  subchannel.data           = &subchannel_info;

  if(ioctl(_obj->gen.fd, CDIOCREADSUBCHANNEL, &subchannel) < 0) {
    perror("CDIOCREADSUBCHANNEL");
    return NULL;
  }

  /* Probably need a loop over tracks rather than give up if we 
     can't find in track 0.
   */
  if (subchannel_info.what.media_catalog.mc_valid)
    return strdup(subchannel_info.what.media_catalog.mc_number);
  else 
    return NULL;
#else 
    return NULL;
#endif

}

/*!
  Return the number of tracks in the current medium.
  CDIO_INVALID_TRACK is returned on error.
*/
static track_t
_get_num_tracks_freebsd(void *env) 
{
  _img_private_t *_obj = env;
  
  if (!_obj->toc_init) _cdio_read_toc (_obj) ;

  return TOTAL_TRACKS;
}

/*!  
  Get format of track. 

  FIXME: We're just guessing this from the GNU/Linux code.
  
*/
static track_format_t
_get_track_format_freebsd(void *env, track_t i_track) 
{
  _img_private_t *_obj = env;

  /* This is pretty much copied from the "badly broken" cdrom_count_tracks
     in linux/cdrom.c.
   */
  if (_obj->tocent[i_track-FIRST_TRACK_NUM].entry.control & CDIO_CDROM_DATA_TRACK) {
    if (_obj->tocent[i_track-FIRST_TRACK_NUM].address_format == 0x10)
      return TRACK_FORMAT_CDI;
    else if (_obj->tocent[i_track-FIRST_TRACK_NUM].address_format == 0x20) 
      return TRACK_FORMAT_XA;
    else
      return TRACK_FORMAT_DATA;
  } else
    return TRACK_FORMAT_AUDIO;
  
}

/*!
  Return true if we have XA data (green, mode2 form1) or
  XA data (green, mode2 form2). That is track begins:
  sync - header - subheader
  12     4      -  8

  FIXME: there's gotta be a better design for this and get_track_format?
*/
static bool
_get_track_green_freebsd(void *env, track_t i_track) 
{
  _img_private_t *_obj = env;
  
  if (i_track == CDIO_CDROM_LEADOUT_TRACK) i_track = TOTAL_TRACKS+1;

  if (i_track > TOTAL_TRACKS+1 || i_track == 0)
    return false;

  /* FIXME: Dunno if this is the right way, but it's what 
     I was using in cdinfo for a while.
   */
  return ((_obj->tocent[i_track-FIRST_TRACK_NUM].entry.control & 2) != 0);
}

/*!  
  Return the starting LSN track number
  i_track in obj.  Track numbers start at 1.
  The "leadout" track is specified either by
  using i_track LEADOUT_TRACK or the total tracks+1.
  False is returned if there is no track entry.
*/
static lba_t
_get_track_lba_freebsd(void *env, track_t i_track)
{
  _img_private_t *_obj = env;

  if (!_obj->toc_init) _cdio_read_toc (_obj) ;

  if (i_track == CDIO_CDROM_LEADOUT_TRACK) i_track = TOTAL_TRACKS+1;

  if (i_track > TOTAL_TRACKS+1 || i_track == 0) {
    return CDIO_INVALID_LBA;
  } else {
    return cdio_lsn_to_lba(ntohl(_obj->tocent[i_track-FIRST_TRACK_NUM].entry.addr.lba));
  }
}

#endif /* HAVE_FREEBSD_CDROM */

/*!
  Return an array of strings giving possible CD devices.
 */
char **
cdio_get_devices_freebsd (void)
{
#ifndef HAVE_FREEBSD_CDROM
  return NULL;
#else
  char drive[40];
  char **drives = NULL;
  unsigned int num_drives=0;
  bool exists=true;
  char c;
  
  /* Scan the system for CD-ROM drives.
  */

#ifdef USE_ETC_FSTAB

  struct fstab *fs;
  setfsent();
  
  /* Check what's in /etc/fstab... */
  while ( (fs = getfsent()) )
    {
      if (strncmp(fs->fs_spec, "/dev/sr", 7))
	cdio_add_device_list(&drives, fs->fs_spec, &num_drives);
    }
  
#endif

  /* Scan the system for CD-ROM drives.
     Not always 100% reliable, so use the USE_MNTENT code above first.
  */
  for ( c='0'; exists && c <='9'; c++ ) {
    sprintf(drive, "/dev/acd%cc", c);
    exists = cdio_is_cdrom(drive, NULL);
    if ( exists ) {
      cdio_add_device_list(&drives, drive, &num_drives);
    }
  }
  cdio_add_device_list(&drives, NULL, &num_drives);
  return drives;
#endif /*HAVE_FREEBSD_CDROM*/
}

/*!
  Return a string containing the default CD device if none is specified.
 */
char *
cdio_get_default_device_freebsd()
{
  return strdup(DEFAULT_CDIO_DEVICE);
}

/*!
  Initialization routine. This is the only thing that doesn't
  get called via a function pointer. In fact *we* are the
  ones to set that up.
 */
CdIo *
cdio_open_freebsd (const char *psz_source_name)
{
  return cdio_open_am_freebsd(psz_source_name, NULL);
}


/*!
  Initialization routine. This is the only thing that doesn't
  get called via a function pointer. In fact *we* are the
  ones to set that up.
 */
CdIo *
cdio_open_am_freebsd (const char *psz_orig_source_name, 
		      const char *psz_access_mode)
{

#ifdef HAVE_FREEBSD_CDROM
  CdIo *ret;
  _img_private_t *_data;
  char *psz_source_name;

  cdio_funcs _funcs = {
    .eject_media        = _eject_media_freebsd,
    .free               = _free_freebsd,
    .get_arg            = _get_arg_freebsd,
    .get_default_device = cdio_get_default_device_freebsd,
    .get_devices        = cdio_get_devices_freebsd,
    .get_drive_cap      = NULL,
    .get_first_track_num= _get_first_track_num_freebsd,
    .get_mcn            = _get_mcn_freebsd,
    .get_num_tracks     = _get_num_tracks_freebsd,
    .get_track_format   = _get_track_format_freebsd,
    .get_track_green    = _get_track_green_freebsd,
    .get_track_lba      = _get_track_lba_freebsd, 
    .get_track_msf      = NULL,
    .lseek              = cdio_generic_lseek,
    .read               = cdio_generic_read,
    .read_audio_sectors = _read_audio_sectors_freebsd,
    .read_mode2_sector  = _read_mode2_sector_freebsd,
    .read_mode2_sectors = _read_mode2_sectors_freebsd,
    .set_arg            = _set_arg_freebsd,
    .stat_size          = _stat_size_freebsd
  };

  _data                 = _cdio_malloc (sizeof (_img_private_t));
  _data->access_mode    = str_to_access_mode_freebsd(psz_access_mode);
  _data->gen.init       = false;
  _data->gen.fd         = -1;

  if (NULL == psz_orig_source_name) {
    psz_source_name=cdio_get_default_device_freebsd();
    _data->device  = psz_source_name;
    _set_arg_freebsd(_data, "source", psz_source_name);
  } else {
    if (cdio_is_device_generic(psz_orig_source_name)) {
      _set_arg_freebsd(_data, "source", psz_orig_source_name);
      _data->device  = strdup(psz_orig_source_name);
    } else {
      /* The below would be okay if all device drivers worked this way. */
#if 0
      cdio_info ("source %s is a not a device", psz_orig_source_name);
#endif
      return NULL;
    }
  }
    
  ret = cdio_new (_data, &_funcs);
  if (ret == NULL) return NULL;

  if (cdio_generic_init(_data))
    if ( _data->access_mode == _AM_IOCTL ) {
      return ret;
    } else {
      if (init_freebsd_cam(_data)) 
	return ret;
      else {
	cdio_generic_free (_data);
	return NULL;
      }
    }
  else {
    cdio_generic_free (_data);
    return NULL;
  }
  
#else 
  return NULL;
#endif /* HAVE_FREEBSD_CDROM */

}

bool
cdio_have_freebsd (void)
{
#ifdef HAVE_FREEBSD_CDROM
  return true;
#else 
  return false;
#endif /* HAVE_FREEBSD_CDROM */
}