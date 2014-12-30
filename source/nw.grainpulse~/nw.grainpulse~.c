/*
** nw.grainpulse~.c
**
** MSP object
** sends out a single grains when it receives a pulse
** 2001/08/29 started by Nathan Wolek
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

#define OBJECT_NAME		"nw.grainpulse~"		// name of the object

/* for the assist method */
#define ASSIST_INLET	1
#define ASSIST_OUTLET	2

/* for direction flag */
#define FORWARD_GRAINS		0
#define REVERSE_GRAINS		1

/* for interpolation flag */
#define INTERP_OFF			0
#define INTERP_ON			1

/* for overflow flag, added 2002.10.28 */
#define OVERFLOW_OFF		0
#define OVERFLOW_ON			1

static t_class *grainpulse_class;		// required global pointing to this class

typedef struct _grainpulse
{
	t_pxobject x_obj;					// <--
	// sound buffer info
	t_symbol *snd_sym;
	t_buffer *snd_buf_ptr;
	t_buffer *next_snd_buf_ptr;		//added 2002.07.25
	//double snd_last_out; removed 2005.01.25
	//long snd_buf_length;	//removed 2002.07.11
	short snd_interp;
	// window buffer info
	t_symbol *win_sym;
	t_buffer *win_buf_ptr;
	t_buffer *next_win_buf_ptr;		//added 2002.07.25
	//double win_last_out; removed 2005.01.25
	//long win_buf_length;	//removed 2002.07.11
	short win_interp;
	// current grain info
	double grain_pos_start;	// in samples
	double grain_length;	// in milliseconds
	double grain_pitch;		// as multiplier
	double grain_gain;		// linear gain mult; add 2008.04.22
	double grain_sound_length;	// in milliseconds
	double win_step_size;	// in samples
	double snd_step_size;	// in samples
	double curr_win_pos;	// in samples
	double curr_snd_pos;	// in samples
	short grain_direction;	// forward or reverse
	short overflow_status;	//added 2002.10.28, only used while grain is sounding // <--
			//will produce false positives otherwise
	// defered grain info at control rate
	double next_grain_pos_start;	// in milliseconds
	double next_grain_length;		// in milliseconds
	double next_grain_pitch;		// as multiplier
	double next_grain_gain;			// linear gain mult; add 2008.04.22
	short next_grain_direction;		// forward or reverse
	// signal or control grain info
	short grain_pos_start_connected;	// <--
	short grain_length_connected;		// <--
	short grain_pitch_connected;		// <--
	short grain_gain_connected;			// add 2008.04.22
	// grain tracking info
	short grain_stage;
	long curr_grain_samp;				// removed 2003.08.03
	float last_pulse_in;				// <--
	double output_sr;					// <--
	double output_1oversr;				// <--
	//bang on init outlet, added 2004.03.10
	void *out_reportoninit;
	t_symbol *ts_offset;
	t_symbol *ts_dur;
	t_symbol *ts_pscale;
} t_grainpulse;

void *grainpulse_new(t_symbol *snd, t_symbol *win);
t_int *grainpulse_perform(t_int *w);
t_int *grainpulse_perform0(t_int *w);
void grainpulse_initGrain(t_grainpulse *x, float in_pos_start, float in_length, 
		float in_pitch_mult, float in_gain_mult);
void grainpulse_reportoninit(t_grainpulse *x, t_symbol *s, short argc, t_atom argv);
void grainpulse_dsp(t_grainpulse *x, t_signal **sp, short *count);
void grainpulse_setsnd(t_grainpulse *x, t_symbol *s);
void grainpulse_setwin(t_grainpulse *x, t_symbol *s);
void grainpulse_float(t_grainpulse *x, double f);
void grainpulse_int(t_grainpulse *x, long l);
void grainpulse_sndInterp(t_grainpulse *x, long l);
void grainpulse_winInterp(t_grainpulse *x, long l);
void grainpulse_reverse(t_grainpulse *x, long l);
void grainpulse_assist(t_grainpulse *x, t_object *b, long msg, long arg, char *s);
void grainpulse_getinfo(t_grainpulse *x);
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
    
    c = class_new(OBJECT_NAME, (method)grainpulse_new, (method)dsp_free,
			(short)sizeof(t_grainpulse), 0L, A_SYM, A_SYM, 0);
    class_dspinit(c); // add standard functions to class
    
	class_addmethod(c, (method)grainpulse_dsp, "dsp", A_CANT, 0);
	
	/* bind method "grainpulse_setsnd" to the 'setSound' message */
	class_addmethod(c, (method)grainpulse_setsnd, "setSound", A_SYM, 0);
	
	/* bind method "grainpulse_setwin" to the 'setWin' message */
	class_addmethod(c, (method)grainpulse_setwin, "setWin", A_SYM, 0);
	
	/* bind method "grainpulse_float" to incoming floats */
	class_addmethod(c, (method)grainpulse_float, "float", A_FLOAT, 0);
	
	/* bind method "grainpulse_int" to incoming ints */
	class_addmethod(c, (method)grainpulse_int, "int", A_LONG, 0);
	
	/* bind method "grainpulse_reverse" to the direction message */
	class_addmethod(c, (method)grainpulse_reverse, "reverse", A_LONG, 0);
	
	/* bind method "grainpulse_sndInterp" to the sndInterp message */
	class_addmethod(c, (method)grainpulse_sndInterp, "sndInterp", A_LONG, 0);
	
	/* bind method "grainpulse_winInterp" to the winInterp message */
	class_addmethod(c, (method)grainpulse_winInterp, "winInterp", A_LONG, 0);
	
	/* bind method "grainpulse_assist" to the assistance message */
	class_addmethod(c, (method)grainpulse_assist, "assist", A_CANT, 0);
	
	/* bind method "grainpulse_getinfo" to the getinfo message */
	class_addmethod(c, (method)grainpulse_getinfo, "getinfo", A_NOTHING, 0);
	
    class_register(CLASS_BOX, c); // register the class w max
    grainpulse_class = c;
    
    /* needed for 'buffer~' work, checks for validity of buffer specified */
    ps_buffer = gensym("buffer~");
    
    #ifdef DEBUG
        post("%s: main function was called", OBJECT_NAME);
    #endif /* DEBUG */
    
    return 0;
}

/********************************************************************************
void *grainpulse_new(double initial_pos)

inputs:			*snd		-- name of buffer holding sound
				*win		-- name of buffer holding window
description:	called for each new instance of object in the MAX environment;
		defines inlets and outlets; sets variables and buffers
returns:		nothing
********************************************************************************/
void *grainpulse_new(t_symbol *snd, t_symbol *win)
{
	t_grainpulse *x = (t_grainpulse *)newobject(this_class);
	dsp_setup((t_pxobject *)x, 5);					// five inlets; change 2008.04.22
	x->out_reportoninit = outlet_new((t_pxobject *)x, 0L);	// report settings outlet
			// added 2004.03.22
	outlet_new((t_pxobject *)x, "signal");			// one outlet
	outlet_new((t_pxobject *)x, "signal");			// second outlet, added 2002.10.23
	
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
	x->grain_gain = x->next_grain_gain = 1.0;
	x->win_step_size = x->snd_step_size = 0.0;
	x->curr_snd_pos = 0.0;
	x->curr_win_pos = 0.0;
	x->last_pulse_in = 0.0;
	
	/* setup t_symbols for output messages (saves overhead)*/
	x->ts_offset = gensym("offset");
	x->ts_dur = gensym("dur");
	x->ts_pscale = gensym("pscale");
	
	/* set flags to defaults */
	x->snd_interp = INTERP_ON;
	x->win_interp = INTERP_ON;
	x->grain_direction = x->next_grain_direction = FORWARD_GRAINS;
	
	x->x_obj.z_misc = Z_NO_INPLACE;
	
	/* return a pointer to the new object */
	return (x);
}

/********************************************************************************
void grainpulse_dsp(t_cpPan *x, t_signal **sp, short *count)

inputs:			x		-- pointer to this object
				sp		-- array of pointers to input & output signals
				count	-- array of shorts detailing number of signals attached
					to each inlet
description:	called when DSP call chain is built; adds object to signal flow
returns:		nothing
********************************************************************************/
void grainpulse_dsp(t_grainpulse *x, t_signal **sp, short *count)
{
	/* set buffers */
	grainpulse_setsnd(x, x->snd_sym);
	grainpulse_setwin(x, x->win_sym);
	
	/* test inlets for signal data */
	x->grain_pos_start_connected = count[1];
	x->grain_length_connected = count[2];
	x->grain_pitch_connected = count[3];
	x->grain_gain_connected = count[4];	// add 2008.04.22; incremented num below
	
	x->output_sr = sp[5]->s_sr;
	x->output_1oversr = 1.0 / x->output_sr;
	
	//set overflow status, added 2002.10.28
	x->overflow_status = OVERFLOW_OFF;
	
	if (count[5] && count[0]) {	// if input and output connected..
		// output is computed
		dsp_add(grainpulse_perform, 9, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, 
			sp[3]->s_vec, sp[4]->s_vec, sp[5]->s_vec, sp[6]->s_vec, sp[5]->s_n);
		#ifdef DEBUG
			post("%s: output is being computed", OBJECT_NAME);
		#endif /* DEBUG */
	} else {					// if not...
		// no output computed
		//dsp_add(grainpulse_perform0, 2, sp[4]->s_vec, sp[4]->s_n);
		#ifdef DEBUG
			post("%s: no output computed", OBJECT_NAME);
		#endif /* DEBUG */
	}
	
}

/********************************************************************************
t_int *grainpulse_perform(t_int *w)

inputs:			w		-- array of signal vectors specified in "grainpulse_dsp"
description:	called at interrupt level to compute object's output; used when
		outlets are connected; tests inlet 2 3 & 4 to use either control or audio
		rate data
returns:		pointer to the next 
********************************************************************************/
t_int *grainpulse_perform(t_int *w)
{
	t_grainpulse *x = (t_grainpulse *)(w[1]);
	float *in_pulse = (float *)(w[2]);
	float *in_pos_start = (float *)(w[3]);
	float *in_length = (float *)(w[4]);
	float *in_pitch_mult = (float *)(w[5]);
	float *in_gain_mult = (float *)(w[6]);	// add 2008.04.22
	t_float *out = (t_float *)(w[7]);
	t_float *out2 = (t_float *)(w[8]); 	//overflow, added 2002.10.23
	int vec_size = (int)(w[9]);
	t_buffer *s_ptr = x->snd_buf_ptr;
	t_buffer *w_ptr = x->win_buf_ptr;
	float *tab_s, *tab_w;
	double s_step_size, w_step_size;
	double  snd_out, win_out, gain_scale; //, last_s, last_w; removed 2005.01.25; add gain_scale 2008.04.22
	double index_s, index_w, temp_index_frac;
	long size_s, size_w, saveinuse_s, saveinuse_w, temp_index_int;
	short interp_s, interp_w, g_direction, of_status;	//of_status added 2002.10.28
	float last_pulse;
	
	vec_size += 1;		//increase by one for pre-decrement
	--out;				//decrease by one for pre-increment
	--out2;				//added 2002.10.23
	
	/* check to make sure buffers are loaded with proper file types*/
	if (x->x_obj.z_disabled)						// object is enabled
		goto out;
	if ((s_ptr == NULL) || (w_ptr == NULL))		// buffer pointers are defined
		goto zero;
	if (!s_ptr->b_valid || !w_ptr->b_valid)		// files are loaded
		goto zero;
		
	// set "in use" to true; added 2005.01.25
	saveinuse_s = s_ptr->b_inuse;
	s_ptr->b_inuse = true;
	saveinuse_w = w_ptr->b_inuse;
	w_ptr->b_inuse = true;
		
	// get interpolation options
	interp_s = x->snd_interp;
	interp_w = x->win_interp;
	
	// get grain options
	g_direction = x->grain_direction;
	of_status = x->overflow_status;	//added 2002.10.28
	// get pointer info
	s_step_size = x->snd_step_size;
	w_step_size = x->win_step_size;
	index_s = x->curr_snd_pos;
	index_w = x->curr_win_pos;
	// get buffer info
	tab_s = s_ptr->b_samples;
	tab_w = w_ptr->b_samples;
	size_s = s_ptr->b_frames;
	size_w = w_ptr->b_frames;
	//last_s = x->snd_last_out;	 removed 2005.01.25
	//last_w = x->win_last_out;  removed 2005.01.25
	last_pulse = x->last_pulse_in;
	gain_scale = x->grain_gain;		//add 2008.04.22
	
	while (--vec_size) {
		
		/* check bounds of window index */
		if (index_w > (size_w - w_step_size)) {
			if (last_pulse == 0.0 && *in_pulse == 1.0) { // if pulse begins...
				//reset in use; 2006.11.22
				s_ptr->b_inuse = saveinuse_s;
				w_ptr->b_inuse = saveinuse_w;
				
				grainpulse_initGrain(x, *in_pos_start, *in_length, *in_pitch_mult, *in_gain_mult);
				
				// get grain option settings
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
				//last_s = x->snd_last_out;  removed 2005.01.25
				//last_w = x->win_last_out;	 removed 2005.01.25
				//last_pulse = *in_pulse; //redundant, line 371, 2002.10.23
				gain_scale = x->grain_gain;		//add 2008.04.22
				
				// set "in use" back to true; 2006.11.22
				saveinuse_s = s_ptr->b_inuse;
				s_ptr->b_inuse = true;
				saveinuse_w = w_ptr->b_inuse;
				w_ptr->b_inuse = true;
				
				//pulse tracking for overflow, added 2002.10.28
				of_status = OVERFLOW_OFF;
			} else {
				*++out = 0.0;
				*++out2 = 0.0;	//added 2002.10.23
				last_pulse = *in_pulse;
				++in_pulse, ++in_pos_start, ++in_length, ++in_pitch_mult, ++in_gain_mult;
				continue;
			}
		}
		
		//pulse tracking for overflow, added 2002.10.29
		if (!of_status) {
			if (last_pulse == 1.0 && *in_pulse == 0.0) { // if grain on & pulse ends...
				of_status = OVERFLOW_ON;	//start overflowing
			}
		}
		
		index_w += w_step_size;			// add a step
		
		/* advance index of sound buffer */
		if (g_direction == FORWARD_GRAINS) {	// if forward...
			index_s += s_step_size;		// add to sound index
		} else {	// if reverse...
			index_s -= s_step_size;		// subtract from sound index
		}
		
		/* check bounds of sound index; wraps if not within bounds */
		while (index_s < 0.0)
			index_s += size_s;
		while (index_s >= size_s)
			index_s -= size_s;
		
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
		*++out = snd_out * win_out * gain_scale;	// mod 2008.04.22
		
		if (of_status) {
			*++out2 = *in_pulse;
		} else {
			*++out2 = 0.0;
		}
		
		// update history
		last_pulse = *in_pulse;
		//last_s = snd_out; removed 2005.01.25
		//last_w = win_out; removed 2005.01.25
		
		// advance other pointers
		++in_pulse, ++in_pos_start, ++in_length, ++in_pitch_mult, ++in_gain_mult;
		
	}
	
	/* update last output variables */
	//x->snd_last_out = last_s; removed 2005.01.25
	//x->win_last_out = last_w; removed 2005.01.25
	x->curr_snd_pos = index_s;
	x->curr_win_pos = index_w;
	x->last_pulse_in = last_pulse;
	x->overflow_status = of_status;

	// reset "in use"; added 2005.01.25
	s_ptr->b_inuse = saveinuse_s;
	w_ptr->b_inuse = saveinuse_w;

	return (w + 10);

zero:
		while (--vec_size) {
			*++out = 0.0;
			*++out2 = -1.0;
		}
out:
		return (w + 10);
}	

/********************************************************************************
t_int *grainpulse_perform0(t_int *w)

inputs:			w		-- array of signal vectors specified in "grainpulse_dsp"
description:	called at interrupt level to compute object's output; used when
		nothing is connected to output; saves CPU cycles
returns:		pointer to the next 
********************************************************************************/
t_int *grainpulse_perform0(t_int *w)
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
void grainpulse_initGrain(t_grainpulse *x, float in_pos_start, float in_length, 
		float in_pitch_mult, float in_gain_mult)

inputs:			x					-- pointer to this object
				in_pos_start		-- offset within sampled buffer
				in_length			-- length of grain
				in_pitch_mult		-- sample playback speed, 1 = normal
				in_gain_mult		-- scales gain output, 1 = no change
description:	initializes grain vars; called from perform method when pulse is 
		received
returns:		nothing 
********************************************************************************/
void grainpulse_initGrain(t_grainpulse *x, float in_pos_start, float in_length, 
		float in_pitch_mult, float in_gain_mult)
{
	t_buffer *s_ptr;
	t_buffer *w_ptr;
	
	#ifdef DEBUG
		post("%s: initializing grain", OBJECT_NAME);
	#endif /* DEBUG */
	
	if (x->next_snd_buf_ptr != NULL) {	//added 2002.07.24
		x->snd_buf_ptr = x->next_snd_buf_ptr;
		x->next_snd_buf_ptr = NULL;
		//x->snd_last_out = 0.0; removed 2005.01.25
		
		#ifdef DEBUG
			post("%s: sound buffer pointer updated", OBJECT_NAME);
		#endif /* DEBUG */
	}
	if (x->next_win_buf_ptr != NULL) {	//added 2002.07.24
		x->win_buf_ptr = x->next_win_buf_ptr;
		x->next_win_buf_ptr = NULL;
		//x->win_last_out = 0.0; removed 2005.01.25
		
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
	
	// add 2008.04.22
	if (x->grain_gain_connected) { // if gain multiplier is at audio rate
		x->grain_gain = in_gain_mult;
	} else { // if gain multiplier at control rate
		x->grain_gain = x->next_grain_gain;
	}
	
	// compute amount of sound file for grain
	x->grain_sound_length = x->grain_length * x->grain_pitch;
	
	// compute window buffer step size per vector sample 
	x->win_step_size = w_ptr->b_frames / (x->grain_length * x->output_sr * 0.001);
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
	//x->snd_last_out = x->win_last_out = 0.0; removed 2005.01.25
	
	// send report out at beginning of grain
	defer(x, (void *)grainpulse_reportoninit,0L,0,0L); //added 2004.03.10
	
	#ifdef DEBUG
		post("%s: beginning of grain", OBJECT_NAME);
		post("%s: win step size = %f samps", OBJECT_NAME, x->win_step_size);
		post("%s: snd step size = %f samps", OBJECT_NAME, x->snd_step_size);
	#endif /* DEBUG */
}

/********************************************************************************
void grainpulse_reportoninit(t_pulsesamp *x, t_symbol *s, short argc, t_atom argv)

inputs:			x		-- pointer to our object
description:	sends settings when grain is initialized; allows external settings
	to advance; extra arguments allow for use of defer method
returns:		nothing
********************************************************************************/
void grainpulse_reportoninit(t_grainpulse *x, t_symbol *s, short argc, t_atom argv)
{
	t_atom ta_msgvals[3];
	
	SETFLOAT(ta_msgvals, (float) x->grain_pos_start);
	SETFLOAT((ta_msgvals + 1), (float) x->grain_length);
	SETFLOAT((ta_msgvals + 2), (float) x->grain_pitch);
	
	if (sys_getdspstate()) {
		//report settings used in grain production
		outlet_anything(x->out_reportoninit, x->ts_offset, 1, ta_msgvals);
		outlet_anything(x->out_reportoninit, x->ts_dur, 1, (ta_msgvals + 1));
		outlet_anything(x->out_reportoninit, x->ts_pscale, 1, (ta_msgvals + 2));
	}
}

/********************************************************************************
void grainpulse_setsnd(t_index *x, t_symbol *s)

inputs:			x		-- pointer to this object
				s		-- name of buffer to link
description:	links buffer holding the grain sound source 
returns:		nothing
********************************************************************************/
void grainpulse_setsnd(t_grainpulse *x, t_symbol *s)
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
				//x->snd_last_out = 0.0; removed 2005.01.25
				
				#ifdef DEBUG
					post("%s: current sound set to buffer~ > %s <", OBJECT_NAME, s->s_name);
				#endif /* DEBUG */
			} else { // else defer to next buffer
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
void grainpulse_setwin(t_grainpulse *x, t_symbol *s)

inputs:			x		-- pointer to this object
				s		-- name of buffer to link
description:	links buffer holding the grain window 
returns:		nothing
********************************************************************************/
void grainpulse_setwin(t_grainpulse *x, t_symbol *s)
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
				//x->win_last_out = 0.0; removed 2005.01.25
				
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
void grainpulse_float(t_grainpulse *x, double f)

inputs:			x		-- pointer to our object
				f		-- value of float input
description:	handles floats sent to inlets; inlet 2 sets "next_grain_pos_start" 
		variable; inlet 3 sets "next_grain_length" variable; inlet 4 sets 
		"next_grain_pitch" variable; left inlet generates error message in max 
		window
returns:		nothing
********************************************************************************/
void grainpulse_float(t_grainpulse *x, double f)
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
	else if (x->x_obj.z_in == 4) // if inlet 5; add 2008.04.22
	{
		x->next_grain_gain = f;
	}
	else if (x->x_obj.z_in == 0)
	{
		post("%s: left inlet does not accept floats", OBJECT_NAME);
	}
}

/********************************************************************************
void grainpulse_int(t_grainpulse *x, long l)

inputs:			x		-- pointer to our object
				l		-- value of int input
description:	handles int sent to inlets; inlet 2 sets "next_grain_pos_start" 
		variable; inlet 3 sets "next_grain_length" variable; inlet 4 sets 
		"next_grain_pitch" variable; left inlet generates error message in max 
		window
returns:		nothing
********************************************************************************/
void grainpulse_int(t_grainpulse *x, long l)
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
	else if (x->x_obj.z_in == 4) // if inlet 5
	{
		x->next_grain_gain = (double) l;
	}
	else if (x->x_obj.z_in == 0)
	{
		post("%s: left inlet does not accept floats", OBJECT_NAME);
	}
}

/********************************************************************************
void grainpulse_sndInterp(t_grainpulse *x, long l)

inputs:			x		-- pointer to our object
				l		-- flag value
description:	method called when "sndInterp" message is received; allows user 
		to define whether interpolation is used in pulling values from the sound
		buffer; default is on
returns:		nothing
********************************************************************************/
void grainpulse_sndInterp(t_grainpulse *x, long l)
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
void grainpulse_winInterp(t_grainpulse *x, long l)

inputs:			x		-- pointer to our object
				l		-- flag value
description:	method called when "winInterp" message is received; allows user 
		to define whether interpolation is used in pulling values from the window
		buffer; default is off
returns:		nothing
********************************************************************************/
void grainpulse_winInterp(t_grainpulse *x, long l)
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
void grainpulse_reverse(t_grainpulse *x, long l)

inputs:			x		-- pointer to our object
				l		-- flag value
description:	method called when "reverse" message is received; allows user 
		to define whether sound is played forward or reverse; default is forward
returns:		nothing
********************************************************************************/
void grainpulse_reverse(t_grainpulse *x, long l)
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
void grainpulse_assist(t_grainpulse *x, t_object *b, long msg, long arg, char *s)

inputs:			x		-- pointer to our object
				b		--
				msg		--
				arg		--
				s		--
description:	method called when "assist" message is received; allows inlets 
		and outlets to display assist messages as the mouse passes over them
returns:		nothing
********************************************************************************/
void grainpulse_assist(t_grainpulse *x, t_object *b, long msg, long arg, char *s)
{
	if (msg==ASSIST_INLET) {
		switch (arg) {
			case 0:
				strcpy(s, "(signal) pulse to output a grain");
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
			case 4:
				strcpy(s, "(signal/float) grain gain multiplier, 1.0 = unchanged");
				break;
		}
	} else if (msg==ASSIST_OUTLET) {
		switch (arg) {
			case 0:
				strcpy(s, "(signal) grain output");
				break;
			case 1:
				strcpy(s, "(signal) pulse overflow");
				break;
			case 2:
				strcpy(s, "(msg) reports settings used for current grain");
				break;
		}
	}
	
	#ifdef DEBUG
		post("%s: assist message displayed", OBJECT_NAME);
	#endif /* DEBUG */
}

/********************************************************************************
void grainpulse_getinfo(t_grainpulse *x)

inputs:			x		-- pointer to our object
				
description:	method called when "getinfo" message is received; displays info
		about object and last update
returns:		nothing
********************************************************************************/
void grainpulse_getinfo(t_grainpulse *x)
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