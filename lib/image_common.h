/*
    $Id: image_common.h,v 1.4 2004/04/25 14:48:17 rocky Exp $

    Copyright (C) 2004 Rocky Bernstein <rocky@panix.com>

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

/* Common image routines. */

/*!
  Return the media catalog number (MCN) from the CD or NULL if there
  is none or we don't have the ability to get it.

  Note: string is malloc'd so caller has to free() the returned
  string when done with it.
  */
static char *
_get_mcn_image(const void *env)
{
  const _img_private_t *_obj = env;
  
  if (NULL == _obj->mcn) return NULL;
  return strdup(_obj->mcn);
}

/*!  
  Return the starting MSF (minutes/secs/frames) for the track number
  track_num in obj.  Tracks numbers start at 1.
  The "leadout" track is specified either by
  using track_num LEADOUT_TRACK or the total tracks+1.

*/
static bool
_get_track_msf_image(void *env, track_t track_num, msf_t *msf)
{
  _img_private_t *_obj = env;

  if (NULL == msf) return false;

  if (track_num == CDIO_CDROM_LEADOUT_TRACK) track_num = _obj->total_tracks+1;

  if (track_num <= _obj->total_tracks+1 && track_num != 0) {
    *msf = _obj->tocent[track_num-1].start_msf;
    return true;
  } else 
    return false;
}

/*!
  Return the number of of the first track. 
  CDIO_INVALID_TRACK is returned on error.
*/
static track_t
_get_first_track_num_image(void *env) 
{
  _img_private_t *_obj = env;
  
  return _obj->first_track_num;
}

/*!
  Return the number of tracks. We fake it an just say there's
  one big track. 
*/
static track_t
_get_num_tracks_image(void *env) 
{
  _img_private_t *_obj = env;

  return _obj->total_tracks;
}
