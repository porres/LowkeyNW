/*
** nw.grainbang~.c
**
** MSP object
** sends out a single grains when it receives a bang 
** 2001/07/18 started by Nathan Wolek
**
** Copyright © 2002,2014 by Nathan Wolek
** License: http://opensource.org/licenses/BSD-3-Clause
**
*/

#include "ext.h"		// required for all MAX external objects
#include "ext_obex.h"   // required for new style MAX objects
#include "z_dsp.h"		// required for all MSP external objects
#include "buffer.h"		// required to deal with buffer object
#include <string.h>

#define DEBUG			//enable debugging messages

#define OBJECT_NAME		"nw.grainbang~"		// name of the object

/* for the assist method */
#define ASSIST_INLET	1
#define ASSIST_OUTLET	2

/* for the grain stage */
#define NEW_GRAIN		2
#define FINISH_GRAIN	1
#define	NO_GRAIN		0

/* for direction flag */
#define FORWARD_GRAINS		0
#define REVERSE_GRAINS		1

/* for interpolation flag */
#define INTERP_OFF			0
#define INTERP_ON			1

static t_class *grainbang_class;		// required global pointing to this class

typedef struct _grainbang
{
	t_pxobject x_obj;					// <--
	// sound buffer info
	t_symbol *snd_sym;
	t_buffer *snd_buf_ptr;
	t_buffer *next_snd_buf_ptr;		//added 2002.07.24
	//double snd_last_out;	//removed 2005.02.02
	//long snd_buf_length;	//removed 2002.07.11
	short snd_interp;
	// window buffer info
	t_symbol *win_sym;
	t_buffer *win_buf_ptr;
	t_buffer *next_win_buf_ptr;		//added 2002.07.24
	//double win_last_out;	//removed 2005.02.02
	//long win_buf_length;	//removed 2002.07.11
	short win_interp;
	// current grain info
	double grain_pos_start;	// in samples
	double grain_length;	// in milliseconds
	double grain_pitch;		// as multiplier
	double grain_sound_length;	// in milliseconds
	double win_step_size;	// in samples
	double snd_step_size;	// in samples
	double curr_win_pos;	// in samples
	double curr_snd_pos;	// in samples
	short grain_direction;	// forward or reverse
	// defered grain info at control rate
	double next_grain_pos_start;	// in milliseconds
	double next_grain_length;		// in milliseconds
	double next_grain_pitch;		// as multiplier
	short next_grain_direction;		// forward or reverse
	// signal or control grain info
	short grain_pos_start_connected;	// <--
	short grain_length_connected;		// <--
	short grain_pitch_connected;		// <--
	// grain tracking info
	short grain_stage;
	//long curr_grain_samp;				//removed 2003.08.04
	double output_sr;					// <--
	double output_1oversr;				// <--
	//overflow outlet, added 2002.10.23
	void *out_overflow;					// <--
} t_grainbang;

void *grainbang_new(t_symbol *snd, t_symbol *win);
t_int *grainbang_perform(t_int *w);
t_int *grainbang_perform0(t_int *w);
void grainbang_dsp(t_grainbang *x, t_signal **sp, short *count);
void grainbang_setsnd(t_grainbang *x, t_symbol *s);
void grainbang_setwin(t_grainbang *x, t_symbol *s);
void grainbang_float(t_grainbang *x, double f);
void grainbang_int(t_grainbang *x, long l);
void grainbang_bang(t_grainbang *x);
void grainbang_overflow(t_grainbang *x, t_symbol *s, short argc, t_atom argv);
void grainbang_initGrain(t_grainbang *x, float in_pos_start, float in_length, 
		float in_pitch_mult);
void grainbang_sndInterp(t_grainbang *x, long l);
void grainbang_winInterp(t_grainbang *x, long l);
void grainbang_reverse(t_grainbang *x, long l);
void grainbang_assist(t_grainbang *x, t_object *b, long msg, long arg, char *s);
void grainbang_getinfo(t_grainbang *x);
double mcLinearInterp(float *in_array, long index_i, double index_frac, long in_size, short in_chans);

t_symbol *ps_buffer;

/********************************************************************************
 int main(void)
 
 inputs:			nothing
 description:	called the first time the object is used in MAX environment;
 defines inlets, outlets and accepted messages
 returns:		int
 ********************************************************************************/
int C74_EXPORT main(void)
{
    t_class *c;
    
    c = class_new(OBJECT_NAME, (method)grainbang_new, (method)dsp_free,
                  (short)sizeof(t_grainbang), 0L, A_SYM, A_SYM, 0);
    class_dspinit(c); // add standard functions to class

	class_addmethod(c, (method)grainbang_dsp, "dsp", A_CANT, 0);
	
	/* bind method "grainbang_setsnd" to the 'setSound' message */
	class_addmethod(c, (method)grainbang_setsnd, "setSound", A_SYM, 0);
	
	/* bind method "grainbang_setwin" to the 'setWin' message */
	class_addmethod(c, (method)grainbang_setwin, "setWin", A_SYM, 0);
	
	/* bind method "grainbang_float" to incoming floats */
	class_addmethod(c, (method)grainbang_float, "float", A_FLOAT, 0);
	
	/* bind method "grainbang_int" to incoming ints */
	class_addmethod(c, (method)grainbang_int, "int", A_LONG, 0);
	
	/* bind method "grainbang_bang" to incoming bangs */
	class_addmethod(c, (method)grainbang_bang, "bang", 0);
	
	/* bind method "grainbang_reverse" to the direction message */
	class_addmethod(c, (method)grainbang_reverse, "reverse", A_LONG, 0);
	
	/* bind method "grainbang_sndInterp" to the sndInterp message */
	class_addmethod(c, (method)grainbang_sndInterp, "sndInterp", A_LONG, 0);
	
	/* bind method "grainbang_winInterp" to the winInterp message */
	class_addmethod(c, (method)grainbang_winInterp, "winInterp", A_LONG, 0);
	
	/* bind method "grainbang_assist" to the assistance message */
	class_addmethod(c, (method)grainbang_assist, "assist", A_CANT, 0);
	
	/* bind method "grainbang_getinfo" to the getinfo message */
	class_addmethod(c, (method)grainbang_getinfo, "getinfo", A_NOTHING, 0);
	
    /* bind method "grainbang_dsp64" to the dsp64 message */
    //class_addmethod(c, (method)grainbang_dsp64, "dsp64", A_CANT, 0);
    
    class_register(CLASS_BOX, c); // register the class w max
    grainbang_class = c;
    
    /* needed for 'buffer~' work, checks for validity of buffer specified */
    ps_buffer = gensym("buffer~");
    
    #ifdef DEBUG
        post("%s: main function was called", OBJECT_NAME);
    #endif /* DEBUG */
    
    return 0;
}

/********************************************************************************
void *grainbang_new(double initial_pos)

inputs:			*snd		-- name of buffer holding sound
				*win		-- name of buffer holding window
description:	called for each new instance of object in the MAX environment;
		defines inlets and outlets; sets variables and buffers
returns:		nothing
********************************************************************************/
void *grainbang_new(t_symbol *snd, t_symbol *win)
{
	t_grainbang *x = (t_grainbang *) object_alloc((t_class*) grainbang_class);
	dsp_setup((t_pxobject *)x, 5);					// five inlets
	x->out_overflow = bangout((t_pxobject *)x);		// overflow outlet
    outlet_new((t_pxobject *)x, "signal");          // sample count outlet
    outlet_new((t_pxobject *)x, "signal");			// signal ch2 outlet
    outlet_new((t_pxobject *)x, "signal");			// signal ch1 outlet
	
	/* set buffer names */
	x->snd_sym = snd;
	x->win_sym = win;
	
	/* zero pointers */
	x->snd_buf_ptr = x->next_snd_buf_ptr = NULL;
	x->win_buf_ptr = x->next_win_buf_ptr = NULL;
	
	/* setup variables */
	x->grain_pos_start = x->next_grain_pos_start = 0.0;
	x->grain_length = x->next_grain_length = 50.0;
	x->grain_pitch = x->next_grain_pitch = 1.0;
	x->grain_stage = NO_GRAIN;
	x->win_step_size = x->snd_step_size = 0.0;
	x->curr_win_pos = x->curr_snd_pos = 0.0;
	
	/* set flags to defaults */
	x->snd_interp = INTERP_ON;
	x->win_interp = INTERP_ON;
	x->grain_direction = x->next_grain_direction = FORWARD_GRAINS;
	
	x->x_obj.z_misc = Z_NO_INPLACE;
	
	/* return a pointer to the new object */
	return (x);
}

/********************************************************************************
void grainbang_dsp(t_cpPan *x, t_signal **sp, short *count)

inputs:			x		-- pointer to this object
				sp		-- array of pointers to input & output signals
				count	-- array of shorts detailing number of signals attached
					to each inlet
description:	called when DSP call chain is built; adds object to signal flow
returns:		nothing
********************************************************************************/
void grainbang_dsp(t_grainbang *x, t_signal **sp, short *count)
{
    #ifdef DEBUG
        post("%s: adding 32 bit perform method", OBJECT_NAME);
    #endif /* DEBUG */
    
    /* set buffers */
	grainbang_setsnd(x, x->snd_sym);
	grainbang_setwin(x, x->win_sym);
	
	// set stage to no grain
	x->grain_stage = NO_GRAIN;
	
	/* test inlet 2 and 3 for signal data */
	x->grain_pos_start_connected = count[1];
	x->grain_length_connected = count[2];
	x->grain_pitch_connected = count[3];
	
	x->output_sr = sp[4]->s_sr;
	x->output_1oversr = 1.0 / x->output_sr;
	
	if (!count[5]) {	// if output is not connected...
		// nothing is computed
		//dsp_add(grainbang_perform0, 2, sp[4]->s_vec, sp[4]->s_n);
		#ifdef DEBUG
			post("%s: no output computed", OBJECT_NAME);
		#endif /* DEBUG */
	} else {		// if it is...
		// output is computed
		dsp_add(grainbang_perform, 6, x, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec,
			sp[5]->s_vec, sp[5]->s_n);
		#ifdef DEBUG
			post("%s: output is being computed", OBJECT_NAME);
		#endif /* DEBUG */
	}
	
}

/********************************************************************************
t_int *grainbang_perform(t_int *w)

inputs:			w		-- array of signal vectors specified in "grainbang_dsp"
description:	called at interrupt level to compute object's output; used when
		outlets are connected; tests inlet 2 3 & 4 to use either control or audio
		rate data
returns:		pointer to the next 
********************************************************************************/
t_int *grainbang_perform(t_int *w)
{
	t_grainbang *x = (t_grainbang *)(w[1]);
	float in_pos_start = *(float *)(w[2]);
	float in_length = *(float *)(w[3]);
	float in_pitch_mult = *(float *)(w[4]);
	t_float *out = (t_float *)(w[5]);
	int vec_size = (int)(w[6]);
	t_buffer *s_ptr = x->snd_buf_ptr;
	t_buffer *w_ptr = x->win_buf_ptr;
	float *tab_s, *tab_w;
	double s_step_size, w_step_size;
	double  snd_out, win_out; //last_s, last_w;	//removed 2005.02.02
	double index_s, index_w, temp_index_frac;
	long size_s, size_w, saveinuse_s, saveinuse_w, temp_index_int;
	short interp_s, interp_w, g_direction;
	
	vec_size += 1;		//increase by one for pre-decrement
	--out;				//decrease by one for pre-increment
	
	/* check to make sure buffers are loaded with proper file types*/
	if (x->x_obj.z_disabled)					// object is enabled
		goto out;
	if ((s_ptr == NULL) || (w_ptr == NULL))		// buffer pointers are defined
		goto zero;
	if (!s_ptr->b_valid || !w_ptr->b_valid)		// files are loaded
		goto zero;
	
	// set "in use" to true; added 2005.02.03
	saveinuse_s = s_ptr->b_inuse;
	s_ptr->b_inuse = true;
	saveinuse_w = w_ptr->b_inuse;
	w_ptr->b_inuse = true;
	
	// get interpolation options
	interp_s = x->snd_interp;
	interp_w = x->win_interp;
	
	// get grain options
	g_direction = x->grain_direction;
	// get pointer info
	s_step_size = x->snd_step_size;
	w_step_size = x->win_step_size;
	index_s = x->curr_snd_pos;
	index_w = x->curr_win_pos;
	// get buffer info
	tab_s = s_ptr->b_samples;
	size_s = s_ptr->b_frames;
	//last_s = x->snd_last_out;	//removed 2005.02.02
	
	tab_w = w_ptr->b_samples;
	size_w = w_ptr->b_frames;
	//last_w = x->win_last_out;	//removed 2005.02.02
	
	while (--vec_size) {
	
		/* check bounds of window index */
		if (index_w > size_w) {
			if (x->grain_stage == NEW_GRAIN) { // if bang...
				
				// restore buffer in use state; 2006.11.22
				s_ptr->b_inuse = saveinuse_s;
				w_ptr->b_inuse = saveinuse_w;
				
				grainbang_initGrain(x, in_pos_start, in_length, in_pitch_mult);
				
				// get grain options
				g_direction = x->grain_direction;
				// get pointer info
				s_step_size = x->snd_step_size;
				w_step_size = x->win_step_size;
				index_s = x->curr_snd_pos;
				index_w = x->curr_win_pos;
				// get buffer info
				s_ptr = x->snd_buf_ptr;
				w_ptr = x->win_buf_ptr;
				tab_s = s_ptr->b_samples;
				tab_w = w_ptr->b_samples;
				size_s = s_ptr->b_frames;
				size_w = w_ptr->b_frames;
				//last_s = x->snd_last_out;	//removed 2005.02.02
				//last_w = x->win_last_out;	//removed 2005.02.02
				
				// save buffer in use state; set to true; 2006.11.22
				saveinuse_s = s_ptr->b_inuse;
				s_ptr->b_inuse = true;
				saveinuse_w = w_ptr->b_inuse;
				w_ptr->b_inuse = true;
				
				/* move to next stage */
				x->grain_stage = FINISH_GRAIN;
			} else { // if not...
				*++out = 0.0;
				continue;
			}
		}
		
		/* advance index of buffers */
		if (g_direction == FORWARD_GRAINS) {	// if forward...
			index_s += s_step_size;		// add to sound index
		} else {	// if reverse...
			index_s -= s_step_size;		// subtract from sound index
		}
		index_w += w_step_size;			// add a step
		
		/* check bounds of sound index; wraps if not within bounds */
		while (index_s < 0.0)
			index_s += size_s;
		while (index_s >= size_s)
			index_s -= size_s;
		
		/* check bounds of window index */
		if (index_w > size_w) {
			x->grain_stage = NO_GRAIN;
			*++out = 0.0;
			#ifdef DEBUG
				post("%s: end of grain", OBJECT_NAME);
			#endif /* DEBUG */
			continue;
		}
		
		//WINDOW OUT
		
		/* handle temporary vars for interpolation */
		temp_index_int = (long)(index_w); // integer portion of index
		temp_index_frac = index_w - (double)temp_index_int; // fractional portion of index
		
		/*
		if (nc_w > 1) // if buffer has multiple channels...
		{
			// get index to sample from within the interleaved frame
			temp_index_int = temp_index_int * nc_w + chan_w;
		}
		*/
		
		switch (interp_w) {
			case INTERP_ON:
				// perform linear interpolation on window buffer output
				win_out = mcLinearInterp(tab_w, temp_index_int, temp_index_frac, size_w, 1);
				break;
			case INTERP_OFF:
				// interpolation sounds better than following, but uses more CPU
				win_out = tab_w[temp_index_int];
				break;
		}
		
		//SOUND OUT
		
		/* handle temporary vars for interpolation */
		temp_index_int = (long)(index_s); // integer portion of index
		temp_index_frac = index_s - (double)temp_index_int; // fractional portion of index
		
		/*
		if (nc_s > 1) // if buffer has multiple channels...
		{
			// get index to sample from within the interleaved frame
			temp_index_int = temp_index_int * nc_s + chan_s;
		}
		*/
		
		switch (interp_s) {
			case INTERP_ON:
				// perform linear interpolation on sound buffer output
				snd_out = mcLinearInterp(tab_s, temp_index_int, temp_index_frac, size_s, 1);
				break;
			case INTERP_OFF:
				// interpolation sounds better than following, but uses more CPU
				snd_out = tab_s[temp_index_int];
				break;
		}
		
		/* multiply snd_out by win_value */
		*++out = snd_out * win_out;
		
		/* update last output variables */
		//last_s = snd_out;	//removed 2005.02.02
		//last_w = win_out;	//removed 2005.02.02
	}	
	
	/* update last output variables */
	//x->snd_last_out = last_s;	//removed 2005.02.02
	//x->win_last_out = last_w;	//removed 2005.02.02
	x->curr_snd_pos = index_s;
	x->curr_win_pos = index_w;

	// reset "in use"; added 2005.02.03
	s_ptr->b_inuse = saveinuse_s;
	w_ptr->b_inuse = saveinuse_w;
		
	return (w + 7);

zero:
		while (--vec_size) *++out = 0.0;
out:
		return (w + 7);
}	

/********************************************************************************
t_int *grainbang_perform0(t_int *w)

inputs:			w		-- array of signal vectors specified in "grainbang_dsp"
description:	called at interrupt level to compute object's output; used when
		nothing is connected to output; saves CPU cycles
returns:		pointer to the next 
********************************************************************************/
t_int *grainbang_perform0(t_int *w)
{
	t_float *out = (t_float *)(w[1]);
	int vec_size = (int)(w[2]);

	vec_size += 1;		//increase by one for pre-decrement
	--out;				//decrease by one for pre-increment

	while (--vec_size >= 0) {
		*++out = 0.;
	}

	return (w + 3);
}

/********************************************************************************
void grainbang_initGrain(t_grainbang *x, float in_pos_start, float in_length, 
		float in_pitch_mult)

inputs:			x					-- pointer to this object
				in_pos_start		-- offset within sampled buffer
				in_length			-- length of grain
				in_pitch_mult		-- sample playback speed, 1 = normal
description:	initializes grain vars; called from perform method when bang is 
		received
returns:		nothing 
********************************************************************************/
void grainbang_initGrain(t_grainbang *x, float in_pos_start, float in_length, 
		float in_pitch_mult)
{
	t_buffer *s_ptr;
	t_buffer *w_ptr;
	
	#ifdef DEBUG
		post("%s: initializing grain", OBJECT_NAME);
	#endif /* DEBUG */
	
	if (x->next_snd_buf_ptr != NULL) {	//added 2002.07.24
		x->snd_buf_ptr = x->next_snd_buf_ptr;
		x->next_snd_buf_ptr = NULL;
		//x->snd_last_out = 0.0;	//removed 2005.02.02
		
		#ifdef DEBUG
			post("%s: sound buffer pointer updated", OBJECT_NAME);
		#endif /* DEBUG */
	}
	if (x->next_win_buf_ptr != NULL) {	//added 2002.07.24
		x->win_buf_ptr = x->next_win_buf_ptr;
		x->next_win_buf_ptr = NULL;
		//x->win_last_out = 0.0;	//removed 2005.02.02
		
		#ifdef DEBUG
			post("%s: window buffer pointer updated", OBJECT_NAME);
		#endif /* DEBUG */
	}
	
	s_ptr = x->snd_buf_ptr;
	w_ptr = x->win_buf_ptr;
	
	x->grain_direction = x->next_grain_direction;
	
	/* test if variables should be at audio or control rate */
	if (x->grain_length_connected) { // if length is at audio rate
		x->grain_length = in_length;
	} else { // if length is at control rate
		x->grain_length = x->next_grain_length;
	}
	
	if (x->grain_pitch_connected) { // if pitch multiplier is at audio rate
		x->grain_pitch = in_pitch_mult;
	} else { // if pitch multiplier at control rate
		x->grain_pitch = x->next_grain_pitch;
	}
	
	// compute amount of sound file for grain
	x->grain_sound_length = x->grain_length * x->grain_pitch;
	
	// compute window buffer step size per vector sample 
	x->win_step_size = (w_ptr->b_frames) / (x->grain_length * x->output_sr * 0.001);
	// compute sound buffer step size per vector sample
	x->snd_step_size = x->grain_pitch * s_ptr->b_sr * x->output_1oversr;
	
	if (x->grain_pos_start_connected) { // if position is at audio rate
		if (x->grain_direction == FORWARD_GRAINS) {	// if forward...
			x->grain_pos_start = in_pos_start * s_ptr->b_msr;
			x->curr_snd_pos = x->grain_pos_start - x->snd_step_size;
		} else {	// if reverse...
			x->grain_pos_start = (in_pos_start + x->grain_sound_length) * s_ptr->b_msr;
			x->curr_snd_pos = x->grain_pos_start + x->snd_step_size;
		}
	} else { // if position is at control rate
		if (x->grain_direction == FORWARD_GRAINS) {	// if forward...
			x->grain_pos_start = x->next_grain_pos_start * s_ptr->b_msr;
			x->curr_snd_pos = x->grain_pos_start - x->snd_step_size;
		} else {	// if reverse...
			x->grain_pos_start = (x->next_grain_pos_start + x->grain_sound_length) * s_ptr->b_msr;
			x->curr_snd_pos = x->grain_pos_start + x->snd_step_size;
		}
	}
	
	x->curr_win_pos = 0.0 - x->win_step_size;
	// reset history
	//x->snd_last_out = x->win_last_out = 0.0;	//removed 2005.02.02
	
	/* move to next stage */
	//x->grain_stage = FINISH_GRAIN;	//removed 2002.07.15, duplicated in perform method
	
	#ifdef DEBUG
		post("%s: beginning of grain", OBJECT_NAME);
		post("%s: win step size = %f samps", OBJECT_NAME, x->win_step_size);
		post("%s: snd step size = %f samps", OBJECT_NAME, x->snd_step_size);
	#endif /* DEBUG */
}

/********************************************************************************
void grainbang_setsnd(t_index *x, t_symbol *s)

inputs:			x		-- pointer to this object
				s		-- name of buffer to link
description:	links buffer holding the grain sound source 
returns:		nothing
********************************************************************************/
void grainbang_setsnd(t_grainbang *x, t_symbol *s)
{
	t_buffer *b;
	
	if ((b = (t_buffer *)(s->s_thing)) && ob_sym(b) == ps_buffer) {
		if (b->b_nchans != 1) {
			error("%s: buffer~ > %s < must be mono", OBJECT_NAME, s->s_name);
			x->next_snd_buf_ptr = NULL;		//added 2002.07.15
		} else {
			if (x->snd_buf_ptr == NULL) { // if first buffer make current buffer
				x->snd_sym = s;
				x->snd_buf_ptr = b;
				//x->snd_last_out = 0.0;	//removed 2005.02.02
				
				#ifdef DEBUG
					post("%s: current sound set to buffer~ > %s <", OBJECT_NAME, s->s_name);
				#endif /* DEBUG */
			} else { // defer to next buffer
				x->snd_sym = s;
				x->next_snd_buf_ptr = b;
				//x->snd_buf_length = b->b_frames;	//removed 2002.07.11
				//x->snd_last_out = 0.0;		//removed 2002.07.24
				
				#ifdef DEBUG
					post("%s: next sound set to buffer~ > %s <", OBJECT_NAME, s->s_name);
				#endif /* DEBUG */
			}
		}
	} else {
		error("%s: no buffer~ * %s * found", OBJECT_NAME, s->s_name);
		x->next_snd_buf_ptr = NULL;
	}
}

/********************************************************************************
void grainbang_setwin(t_grainbang *x, t_symbol *s)

inputs:			x		-- pointer to this object
				s		-- name of buffer to link
description:	links buffer holding the grain window 
returns:		nothing
********************************************************************************/
void grainbang_setwin(t_grainbang *x, t_symbol *s)
{
	t_buffer *b;
	
	if ((b = (t_buffer *)(s->s_thing)) && ob_sym(b) == ps_buffer) {
		if (b->b_nchans != 1) {
			error("%s: buffer~ > %s < must be mono", OBJECT_NAME, s->s_name);
			x->next_win_buf_ptr = NULL;		//added 2002.07.15
		} else {
			if (x->win_buf_ptr == NULL) { // if first buffer make current buffer
				x->win_sym = s;
				x->win_buf_ptr = b;
				//x->win_last_out = 0.0;	//removed 2005.02.02
				
				/* set current win position to 1 more than length */
				x->curr_win_pos = (float)((x->win_buf_ptr)->b_frames) + 1.0;
				
				#ifdef DEBUG
					post("%s: current window set to buffer~ > %s <", OBJECT_NAME, s->s_name);
				#endif /* DEBUG */
			} else { // else defer to next buffer
				x->win_sym = s;
				x->next_win_buf_ptr = b;
				//x->win_buf_length = b->b_frames;	//removed 2002.07.11
				//x->win_last_out = 0.0;		//removed 2002.07.24
				
				#ifdef DEBUG
					post("%s: next window set to buffer~ > %s <", OBJECT_NAME, s->s_name);
				#endif /* DEBUG */
			}
		}
	} else {
		error("%s: no buffer~ > %s < found", OBJECT_NAME, s->s_name);
		x->next_win_buf_ptr = NULL;
	}
}

/********************************************************************************
void grainbang_float(t_grainbang *x, double f)

inputs:			x		-- pointer to our object
				f		-- value of float input
description:	handles floats sent to inlets; inlet 2 sets "next_grain_pos_start" 
		variable; inlet 3 sets "next_grain_length" variable; inlet 4 sets 
		"next_grain_pitch" variable; left inlet generates error message in max 
		window
returns:		nothing
********************************************************************************/
void grainbang_float(t_grainbang *x, double f)
{
	if (x->x_obj.z_in == 1) // if inlet 2
	{
		x->next_grain_pos_start = f;
	}
	else if (x->x_obj.z_in == 2) // if inlet 3
	{
		if (f > 0.0) {
			x->next_grain_length = f;
		} else {
			post("%s: grain length must be greater than zero", OBJECT_NAME);
		}
	}
	else if (x->x_obj.z_in == 3) // if inlet 4
	{
		x->next_grain_pitch = f;
	}
	else if (x->x_obj.z_in == 0)
	{
		post("%s: left inlet does not accept floats", OBJECT_NAME);
	}
}

/********************************************************************************
void grainbang_int(t_grainbang *x, long l)

inputs:			x		-- pointer to our object
				l		-- value of int input
description:	handles ints sent to inlets; inlet 2 sets "next_grain_pos_start" 
		variable; inlet 3 sets "next_grain_length" variable; inlet 4 sets 
		"next_grain_pitch" variable; left inlet generates error message in max 
		window
returns:		nothing
********************************************************************************/
void grainbang_int(t_grainbang *x, long l)
{
	if (x->x_obj.z_in == 1) // if inlet 2
	{
		x->next_grain_pos_start = (double) l;
	}
	else if (x->x_obj.z_in == 2) // if inlet 3
	{
		if (l > 0) {
			x->next_grain_length = (double) l;
		} else {
			post("%s: grain length must be greater than zero", OBJECT_NAME);
		}
	}
	else if (x->x_obj.z_in == 3) // if inlet 4
	{
		x->next_grain_pitch = (double) l;
	}
	else if (x->x_obj.z_in == 0)
	{
		post("%s: left inlet does not accept floats", OBJECT_NAME);
	}
}

/********************************************************************************
void grainbang_bang(t_grainbang *x)

inputs:			x		-- pointer to our object
description:	handles bangs sent to inlets; inlet 1 creates a grain; all others
	post an error to the max window
returns:		nothing
********************************************************************************/
void grainbang_bang(t_grainbang *x)
{
	if (x->x_obj.z_in == 0) // if inlet 1
	{
		if (x->grain_stage == NO_GRAIN) {
			x->grain_stage = NEW_GRAIN;
			#ifdef DEBUG
				post("%s: grain stage set to new grain", OBJECT_NAME);
			#endif // DEBUG //
		} else {
			defer(x, (void *)grainbang_overflow,0L,0,0L); //added 2002.11.19
		}
	}
	else // all other inlets
	{
		post("%s: that inlet does not accept bangs", OBJECT_NAME);
	}
}

/********************************************************************************
void grainbang_overflow(t_grainbang *x, t_symbol *s, short argc, t_atom argv)

inputs:			x		-- pointer to our object
description:	handles bangs sent to overflow outlet; allows the method to be 
	deferred
returns:		nothing
********************************************************************************/
void grainbang_overflow(t_grainbang *x, t_symbol *s, short argc, t_atom argv)
{
	if (sys_getdspstate()) {
		outlet_bang(x->out_overflow);
	}
}

/********************************************************************************
void grainbang_sndInterp(t_grainbang *x, long l)

inputs:			x		-- pointer to our object
				l		-- flag value
description:	method called when "sndInterp" message is received; allows user 
		to define whether interpolation is used in pulling values from the sound
		buffer; default is on
returns:		nothing
********************************************************************************/
void grainbang_sndInterp(t_grainbang *x, long l)
{
	if (l == INTERP_OFF) {
		x->snd_interp = INTERP_OFF;
		#ifdef DEBUG
			post("%s: sndInterp is set to off", OBJECT_NAME);
		#endif // DEBUG //
	} else if (l == INTERP_ON) {
		x->snd_interp = INTERP_ON;
		#ifdef DEBUG
			post("%s: sndInterp is set to on", OBJECT_NAME);
		#endif // DEBUG //
	} else {
		error("%s: sndInterp message was not understood", OBJECT_NAME);
	}
}

/********************************************************************************
void grainbang_winInterp(t_grainbang *x, long l)

inputs:			x		-- pointer to our object
				l		-- flag value
description:	method called when "winInterp" message is received; allows user 
		to define whether interpolation is used in pulling values from the window
		buffer; default is on
returns:		nothing
********************************************************************************/
void grainbang_winInterp(t_grainbang *x, long l)
{
	if (l == INTERP_OFF) {
		x->win_interp = INTERP_OFF;
		#ifdef DEBUG
			post("%s: winInterp is set to off", OBJECT_NAME);
		#endif // DEBUG //
	} else if (l == INTERP_ON) {
		x->win_interp = INTERP_ON;
		#ifdef DEBUG
			post("%s: winInterp is set to on", OBJECT_NAME);
		#endif // DEBUG //
	} else {
		error("%s: winInterp was not understood", OBJECT_NAME);
	}
}

/********************************************************************************
void grainbang_reverse(t_grainbang *x, long l)

inputs:			x		-- pointer to our object
				l		-- flag value
description:	method called when "reverse" message is received; allows user 
		to define whether sound is played forward or reverse; default is forward
returns:		nothing
********************************************************************************/
void grainbang_reverse(t_grainbang *x, long l)
{
	if (l == REVERSE_GRAINS) {
		x->next_grain_direction = REVERSE_GRAINS;
		#ifdef DEBUG
			post("%s: reverse is set to on", OBJECT_NAME);
		#endif // DEBUG //
	} else if (l == FORWARD_GRAINS) {
		x->next_grain_direction = FORWARD_GRAINS;
		#ifdef DEBUG
			post("%s: reverse is set to off", OBJECT_NAME);
		#endif // DEBUG //
	} else {
		error("%s: reverse was not understood", OBJECT_NAME);
	}
	
}

/********************************************************************************
void grainbang_assist(t_grainbang *x, t_object *b, long msg, long arg, char *s)

inputs:			x		-- pointer to our object
				b		--
				msg		--
				arg		--
				s		--
description:	method called when "assist" message is received; allows inlets 
		and outlets to display assist messages as the mouse passes over them
returns:		nothing
********************************************************************************/
void grainbang_assist(t_grainbang *x, t_object *b, long msg, long arg, char *s)
{
	if (msg==ASSIST_INLET) {
		switch (arg) {
			case 0:
				strcpy(s, "(bang) output a grain");
				break;
			case 1:
				strcpy(s, "(signal/float) sound begin, in milliseconds");
				break;
			case 2:
				strcpy(s, "(signal/float) grain length, in milliseconds");
				break;
			case 3:
				strcpy(s, "(signal/float) grain pitch multiplier, 1.0 = unchanged");
				break;
		}
	} else if (msg==ASSIST_OUTLET) {
		switch (arg) {
			case 0:
				strcpy(s, "(signal) grain output");
				break;
			case 1:
				strcpy(s, "(bang) bang overflow");
				break;
		}
	}
	
	#ifdef DEBUG
		post("%s: assist message displayed", OBJECT_NAME);
	#endif /* DEBUG */
}

/********************************************************************************
void grainbang_getinfo(t_grainbang *x)

inputs:			x		-- pointer to our object
				
description:	method called when "getinfo" message is received; displays info
		about object and lst update
returns:		nothing
********************************************************************************/
void grainbang_getinfo(t_grainbang *x)
{
	post("%s object by Nathan Wolek", OBJECT_NAME);
	post("Last updated on %s - www.nathanwolek.com", __DATE__);
}


/********************************************************************************
double mcLinearInterp(float *in_array, long index_i, double index_frac, 
		long in_size, short in_chans)

inputs:			*in_array -- name of array of input values
				index_i -- index value of sample, specific channel within interleaved frame
				index_frac -- fractional portion of index value for interp
				in_size -- size of input buffer to perform wrapping
				in_chans -- number of channels in input buffer
description:	performs linear interpolation on an input array and to return 
	value of a fractional sample location
returns:		interpolated output
********************************************************************************/
double mcLinearInterp(float *in_array, long index_i, double index_frac, long in_size, short in_chans)
{
	double out, sample1, sample2;
	long index_iP1 = index_i + in_chans;		// corresponding sample in next frame
	
	// make sure that index_iP1 is not out of range
	while (index_iP1 >= in_size) index_iP1 -= in_size;
	
	// get samples
	sample1 = (double)in_array[index_i];
	sample2 = (double)in_array[index_iP1];
	
	//linear interp formula
	out = sample1 + index_frac * (sample2 - sample1);
	
	return out;
}