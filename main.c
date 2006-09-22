/* WaoN - a Wave-to-Notes transcriber : main
 * Copyright (C) 1998-2006 Kengo Ichiki <kichiki@users.sourceforge.net>
 * $Id: main.c,v 1.2 2006/09/22 05:11:35 kichiki Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <math.h>
#include <stdio.h> /* printf(), fprintf(), strerror()  */
#include <sys/errno.h> /* errno  */
#include <stdlib.h> /* exit()  */
#include <string.h> /* strcat(), strcpy()  */

/* FFTW library  */
#ifdef FFTW2
#include <rfftw.h>
#else
#include <fftw3.h>
#endif /* FFTW2 */

#include "sox-wav.h" /* wavstartread(), wavread()  */
#include "midi.h" /* smf_...(), mid2freq[], get_note()  */
#include "fft.h" /* four1()  */
#include "analyse.h" /* note_intensity(), note_on_off(), output_midi()  */

#include "VERSION.h"


void usage (char * argv0)
{
  fprintf (stderr, "WaoN - a Wave-to-Notes transcriber, version %s\n",
	   WAON_VERSION);
  fprintf (stderr, "Copyright (C) 1998-2006 Kengo Ichiki "
	   "<kichiki@users.sourceforge.net>\n\n");
  fprintf (stderr, "Usage: %s [option ...]\n", argv0);
  fprintf (stderr, "  -h --help\tprint this help.\n");
  fprintf (stderr, "  -v --version\tprint version info.\n");
  fprintf (stderr, "OPTIONS FOR FILES\n");
  fprintf (stderr, "  -i --input\tinput wav file (DEFAULT stdin)\n");
  fprintf (stderr, "  -o --output\toutput mid file"
	   " (DEFAULT 'output.mid')\n");
  fprintf (stderr, "\toptions -i and -o have argument '-' "
	   "as stdin/stdout\n");
  fprintf (stderr, "  -p --patch\tpatch file (DEFAULT no patch)\n");
  fprintf (stderr, "NOTE SELECTION OPTIONS\n");
  fprintf (stderr, "  -c --cutoff\tlog10 of cut-off ratio "
	   "to scale velocity of note\n"
	   "\t\t(DEFAULT -5.0)\n");
  fprintf (stderr, "  -r --relative\tlog10 of cut-off ratio "
	   "relative to the average.\n"
	   "\t\t(DEFAULT no relative cutoff\n"
	   "\t\t= absolute cutoff with the value in -c option)\n");
  fprintf (stderr, "  -k --peak\tpeak threshold for note-on, "
	   "which ranges [0,127]\n"
	   "\t\t(DEFAULT 128 = no peak-search = "
	   "search only first on-event)\n");
  fprintf (stderr, "  -t --top\ttop note [midi #] "
	   "(DEFAULT 103 = G8)\n");
  fprintf (stderr, "  -b --bottom\tbottom note [midi #] "
	   "(DEFAULT 28 = E2)\n");
  fprintf (stderr, "\tHere middle C (261 Hz) = C3 = midi 60. "
	   "Midi # ranges [0,127].\n");
  fprintf (stderr, "  -a --adjust\tadjust-pitch param, "
	   "which is suggested by WaoN after analysis.\n"
	   "\t\tunit is half-note, that is, +1 is half-note up,\n"
	   "\t\tand -0.5 is quater-note down. (DEFAULT 0)\n");
  fprintf (stderr, "READING WAV OPTIONS\n");
  fprintf (stderr, "  -n\t\tsampling number from WAV in 1 step "
	   "(DEFAULT 2048)\n");
  fprintf (stderr, "  -s --shift\tshift number from WAV in 1 step\n");
  fprintf (stderr, "\t\t(DEFAULT same in the value in -n option)\n");
  /*fprintf (stderr, "\t\tYou Should Set 2^n for -[nsp] options\n");*/
  fprintf (stderr, "FFT OPTIONS\n");
  fprintf (stderr, "  -w --window\t0 no window (DEFAULT)\n");
  fprintf (stderr, "\t\t1 parzen window\n");
  fprintf (stderr, "\t\t2 welch window\n");
  fprintf (stderr, "\t\t3 hanning window\n");
  fprintf (stderr, "\t\t4 hamming window\n");
  fprintf (stderr, "\t\t5 blackman window\n");
  fprintf (stderr, "\t\t6 steeper 30-dB/octave rolloff window\n");
}


int main (int argc, char** argv)
{
  extern int abs_flg; /* flag for absolute/relative cutoff  */
  extern int peak_threshold; /* to select peaks in a note  */
  extern double adj_pitch;
  extern double pitch_shift;
  extern int n_pitch;

  /* for wav file : soundstream & ft_t is defined in sox-wav.h  */
  struct soundstream informat;
  ft_t ft;

  int show_help;

  char *file_midi = NULL;
  char *file_wav = NULL;
  char *file_patch = NULL;

  int i;
  int icnt; /* counter  */
  int nwin;
  int i0, i1, notetop, notelow;
  double cut_ratio; /* log10 of cutoff ratio for scale velocity  */
  double rel_cut_ratio; /* log10 of cutoff ratio relative to average  */
  double den; /* weight of window function for FFT  */
  double t0; /* time-period for FFT (inverse of smallest frequency)  */
  long *ibuf;
  long *ibuf_shft;
  double *x; /* wave data for FFT  */
  double *y; /* spectrum data for FFT */ 
  double *p; /* power spectrum  */
  long shft, len, div;
  int num, nmidi;

  long *wavbuf;
  long *wavbuf_shft;
  long wavlen, wavshft;

  /* for FFTW library  */
#ifdef FFTW2
  rfftw_plan plan;
#else
  fftw_plan plan;
#endif /* FFTW2 */

  struct ia_note *note_top; /* top of infinite array of note_sig  */
  struct ia_note *notes; /* infinite array of note_sig  */
  int n_notes; /* index within one segment of ia_note  */
  /* intensity matrix  */
  /*char i_lsts[384];*/ /* intensity list for 3 steps  */
  char i_lsts[128]; /* intensity list  */
  char * on_lst[128]; /* on list point to intensity in ia_note array  */


  /* default value */
  cut_ratio = -5.0;
  rel_cut_ratio = 1.0; /* this value is ignored when abs_flg == 1  */
  len = 2048;
  nwin = 0;
  /* for 76 keys piano  */
  notetop = 103; /* G8  */
  notelow = 28; /* E2  */

  abs_flg = 1;

  shft = 0;
  show_help = 0;
  adj_pitch = 0.0;
  peak_threshold = 128; /* this means no peak search  */

  for (i=1; i<argc; i++)
    {
      if ((strcmp (argv[i], "-input" ) == 0)
	 || (strcmp (argv[i], "-i" ) == 0))
	{
	  if ( i+1 < argc )
	    {
	      file_wav = argv[++i];
	    }
	  else
	    {
	      show_help = 1;
	      break;
	    }
	}
      else if ((strcmp (argv[i], "-output" ) == 0)
	      || (strcmp (argv[i], "-o" ) == 0))
	{
	  if ( i+1 < argc )
	    {
	      file_midi = argv[++i];
	    }
	  else
	    {
	      show_help = 1;
	      break;
	    }
	}
      else if ((strcmp (argv[i], "--cutoff") == 0)
	       || (strcmp (argv[i], "-c") == 0))
	{
	  if ( i+1 < argc )
	    {
	      cut_ratio = atof (argv[++i]);
	    }
	  else
	    {
	      show_help = 1;
	      break;
	    }
	}
      else if ((strcmp (argv[i], "--top") == 0)
	       || (strcmp (argv[i], "-t") == 0))
	{
	  if ( i+1 < argc )
	    {
	      notetop = atoi( argv[++i] );
	    }
	  else
	    {
	      show_help = 1;
	      break;
	    }
	}
      else if ((strcmp (argv[i], "--bottom") == 0)
	       || (strcmp (argv[i], "-b") == 0))
	{
	  if ( i+1 < argc )
	    {
	      notelow = atoi (argv[++i]);
	    }
	  else
	    {
	      show_help = 1;
	      break;
	    }
	}
      else if ((strcmp (argv[i], "--window") == 0)
	       || (strcmp (argv[i], "-w") == 0))
	{
	  if ( i+1 < argc )
	    {
	      nwin = atoi (argv[++i]);
	    }
	  else
	    {
	      show_help = 1;
	      break;
	    }
	}
      else if ( strcmp (argv[i], "-n") == 0)
	{
	  if ( i+1 < argc )
	    {
	      len = atoi (argv[++i]);
	    }
	  else
	    {
	      show_help = 1;
	      break;
	    }
	}
      else if ((strcmp (argv[i], "--shift") == 0)
	       || (strcmp (argv[i], "-s") == 0))
	{
	  if ( i+1 < argc )
	    {
	      shft = atoi (argv[++i]);
	    }
	  else
	    {
	      show_help = 1;
	      break;
	    }
	}
      else if ((strcmp (argv[i], "--patch") == 0)
	       || (strcmp (argv[i], "-p") == 0))
	{
	  if ( i+1 < argc )
	    {
	      file_patch = argv[++i];
	    }
	  else
	    {
	      show_help = 1;
	      break;
	    }
	}
      else if ((strcmp (argv[i], "--relative") == 0)
	       || (strcmp (argv[i], "-r") == 0))
	{
	  if ( i+1 < argc )
	    {
	      rel_cut_ratio = atof (argv[++i]);
	      abs_flg = 0;
	    }
	  else
	    {
	      show_help = 1;
	      break;
	    }
	}
      else if ((strcmp (argv[i], "--peak") == 0)
	       || (strcmp (argv[i], "-k") == 0))
	{
	  if ( i+1 < argc )
	    {
	      peak_threshold = atoi (argv[++i]);
	    }
	  else
	    {
	      show_help = 1;
	      break;
	    }
	}
      else if ((strcmp (argv[i], "--adjust") == 0)
	       || (strcmp (argv[i], "-a") == 0))
	{
	  if ( i+1 < argc )
	    {
	      adj_pitch = atof (argv[++i]);
	    }
	  else
	    {
	      show_help = 1;
	      break;
	    }
	}
      else if ((strcmp (argv[i], "--help") == 0)
	       || (strcmp (argv[i], "-h") == 0))
	{
	  show_help = 1;
	  break;
	}
      else
	{
	  show_help = 1;
	}
    }

  if (show_help)
    {
      usage (argv[0]);
      exit (1);
    }

  if (nwin < 0 || nwin > 6)
    {
      nwin = 0;
    }
  if (shft == 0)
    {
      shft = len;
    }


  /* init pointer  */
  ft = &informat;

  /* malloc for note_on_off buffer  */
  note_top = init_ia_note ();
  if (note_top == NULL)
    {
      fprintf(stderr, "cannot note array\n");
      exit (1);
    }
  notes = note_top;
  n_notes = 0;

  /* clear intensity matrix  */
  for (i = 0; i < 128; i++)
    {
      on_lst[i] = NULL;
      i_lsts[i] = 0;
    }

  /* allocate buffers  */
  ibuf = (long *)malloc (sizeof (long) * len);
  if (ibuf == NULL)
    {
      fprintf(stderr, "cannot allocate ibuf[%ld]\n", len);
      exit (1);
    }
  ibuf_shft = (long *)malloc (sizeof (long) * shft);
  if (ibuf_shft == NULL)
    {
      fprintf(stderr, "cannot allocate ibuf_shft[%ld]\n", shft);
      exit (1);
    }
  x = (double *)malloc (sizeof (double) * len);
  if (x == NULL)
    {
      fprintf(stderr, "cannot allocate x[%ld]\n", len);
      exit (1);
    }
  y = (double *)malloc (sizeof (double) * len);
  if (y == NULL)
    {
      fprintf(stderr, "cannot allocate y[%ld]\n", len);
      exit (1);
    }
  p = (double *)malloc (sizeof (double) * (len / 2 + 1));
  if (p == NULL)
    {
      fprintf (stderr, "cannot allocate p[%ld]\n", (len/2+1));
      exit (1);
    }

  /* MIDI output  */
  if (file_midi == NULL)
    {
      file_midi = (char *)malloc (sizeof (char) * (strlen("output.mid") + 1));
      strcpy (file_midi, "output.mid");
    }

  /* open input wav file */
  if (file_wav == NULL
      || strcmp (file_wav, "-") == 0)
    {
      ft->fp = stdin;
    }
  else
    {
      ft->fp = fopen (file_wav, "r");
    }
  if (ft->fp == NULL)
    {
      fprintf (stderr, "Can't open input file %s : %s\n",
	       file_wav, strerror (errno));
      exit (1);
    }
  /* read header of wav file */
  wavstartread (ft);
  /* check stereo */
  if (ft->channels == 2)
    {
      /* allocate buffers for wavread() */
      wavlen = len * ft->channels;
      wavbuf = (long *)malloc (sizeof (long) * wavlen);
      if (wavbuf == NULL)
	{
	  fprintf(stderr, "cannot allocate wavbuf[%ld]\n", wavlen);
	  exit (1);
	}
      wavshft = shft * ft->channels;
      wavbuf_shft = (long *)malloc (sizeof (long) * wavshft);
      if (wavbuf_shft == NULL)
	{
	  fprintf(stderr, "cannot allocate wavbuf_shft[%ld]\n", wavshft);
	  exit (1);
	}
    }
  else if (ft->channels != 1)
    {
      fprintf (stderr, "WaoN Error: only for mono or stereo (%d)\n",
	       ft->channels);
      exit (1);
    }
  else
    {
      wavlen = len;
      wavshft = shft;
      wavbuf      = ibuf;
      wavbuf_shft = ibuf_shft;
    }

  /* time period of FFT  */
  t0 = (double)len/(ft->rate);
  /* window factor for FFT  */
  den = init_den (len, nwin);

  /* set range to analyse (search notes) */
  /* -- after 't0' is calculated  */
  i0 = (int)(mid2freq[notelow]*t0 - 0.5);
  i1 = (int)(mid2freq[notetop]*t0 - 0.5)+1;
  if (i0 < 0)
    {
      i0 = 0;
    }
  if (i1 >= (len/2))
    {
      i1 = len/2 - 1;
    }

  /* init patch  */
  init_patch (file_patch, len, nwin);
  /*                      ^^^ len could be given by option separately  */

  /* initialization plan for FFTW  */
#ifdef FFTW2
  plan = rfftw_create_plan (len, FFTW_REAL_TO_COMPLEX, FFTW_ESTIMATE);
#else
  plan = fftw_plan_r2r_1d (len, x, y, FFTW_R2HC, FFTW_ESTIMATE);
#endif /* FFTW2 */

  /* for first step */
  if (shft != len)
    {
      if (wavread (ft,
		   wavbuf + wavshft,
		   ft->channels * (len - shft))
	  != ft->channels * (len - shft))
	{
	  fprintf (stderr, "No Wav Data!\n");
	  exit(0);
	}
      /* for stereo input */
      if (ft->channels == 2)
	{
	  for (i = len - shft; i < len; i ++)
	    {
	      ibuf [i] = (long)(0.5 *
				((double) wavbuf [i*2 + 0] +
				 (double) wavbuf [i*2 + 1]));
	    }
	}
    }

  /** main loop (icnt) **/
  nmidi = 0; /* number of midi data (note star or stop) */
  pitch_shift = 0.0;
  n_pitch = 0;
  for (icnt=0; ; icnt++)
    {
      /* read from wav */
      /*
      if (wavread (ft, ibuf0, shft) == 0)
	{
	  fprintf (stderr, "WaoN : end of file.\n");
	  break;
	}
      */
      if (wavread (ft, wavbuf_shft, wavshft) != wavshft)
	{
	  fprintf (stderr, "WaoN : end of file.\n");
	  break;
	}
      /* for stereo input */
      if (ft->channels == 2)
	{
	  for (i = 0; i < shft; i ++)
	    {
	      ibuf_shft [i] = (long)(0.5 *
				     ((double) wavbuf_shft [i*2 + 0] +
				      (double) wavbuf_shft [i*2 + 1]));
	    }
	}

      /* shift */
      for (i = 0; i < len - shft; i++)
	{
	  ibuf[i] = ibuf[i + shft];
	}
      for (i = len - shft; i < len; i++)
	{
	  ibuf[i] = ibuf_shft[i - (len - shft)];
	}
      /* set double table x[] for FFT */
      for (i = 0; i < len; i++)
	{
	  x[i] = (double)ibuf[i];
	}

      /* calc power spectrum  */
      power_spectrum_fftw (len, x, y, p, den, nwin, plan);

      note_intensity (len/2, p, cut_ratio, rel_cut_ratio, i0, i1, t0, i_lsts);
      notes = chk_note_on_off (icnt, i_lsts, on_lst,
			       notes, &n_notes, &num);
      nmidi += num;
    }

  pitch_shift /= (double) n_pitch;
  fprintf (stderr, "WaoN : difference of pitch = %f ( + %f )\n",
	   -(pitch_shift - 0.5),
	   adj_pitch);

  /* div is the divisions for one beat (quater-note).
   * here we assume 120 BPM, that is, 1 beat is 0.5 sec.
   * note: (shft / ft->rate) = duration for 1 step (sec) */
  div = (long)(0.5 * (double) (ft->rate) / (double) shft);
  fprintf (stderr, "division = %ld\n", div);
  fprintf (stderr, "WaoN : # of notes = %d\n",nmidi);

  output_midi (nmidi, note_top, div, file_midi);

#ifdef FFTW2
  rfftw_destroy_plan (plan);
#else
  fftw_destroy_plan (plan);
#endif /* FFTW2 */


  free (ibuf);
  free (ibuf_shft);
  free (x);
  free (y);
  free (p);
  free (file_midi);
  if (ft->channels == 2)
    {
      free (wavbuf);
      free (wavbuf_shft);
    }
  return 0;
}
