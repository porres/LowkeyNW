/*
** nw.pulsesamp~.c
**
** MSP object
** play samples from buffer when pulse is received
** 2002/2/24 started by Nathan Wolek, based on bangsamp~ & grain.pulse~
**
** Copyright © 2002,2014 by Nathan Wolek
** License: http://opensource.org/licenses/BSD-3-Clause
**
*/

#include "c74_msp.h"

using namespace c74::max;

//#define DEBUG			//enable debugging messages

#define OBJECT_NAME		"nw.pulsesamp~"		// name of the object

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

/* for overflow flag, added 2002.10.28 */
#define OVERFLOW_OFF		0
#define OVERFLOW_ON			1

static t_class *pulsesamp_class;		// required global pointing to this class

typedef struct _nw_pulsesamp
{
	t_pxobject x_obj;
	// sound buffer info
	t_symbol *snd_sym;
	t_buffer_ref *snd_buf_ptr;
	t_buffer_ref *next_snd_buf_ptr;
	double snd_last_out;
	//long snd_buf_length;	//removed 2002.07.11
	short snd_interp;
	// current grain info
	double grain_samp_inc;		// in buffer_samples/playback_sample
	double grain_gain;	// as coef
	double grain_start;	// in samples; add 2005.10.10
	double grain_end;	// in samples; add 2005.10.10
	short grain_direction;	// forward or reverse
	double snd_step_size;	// in samples
	double curr_snd_pos;	// in samples
	short overflow_status;	//only used while grain is sounding
			//will produce false positives otherwise
	// defered grain info at control rate
	double next_grain_samp_inc;	// in samples/playback_sample
	double next_grain_gain;		// in milliseconds
	double next_grain_start;		// in milliseconds; add 2005.10.10
	double next_grain_end;		// in milliseconds; add 2005.10.10
	short next_grain_direction;	// forward or reverse
	// signal or control grain info
	short grain_samp_inc_connected;
	short grain_gain_connected;
	short grain_start_connected;	// add 2005.10.10
	short grain_end_connected;		// add 2005.10.10
	// grain tracking info
	long curr_count_samp;			// add 2007.04.10
	float last_pulse_in;
	double output_sr;
	double output_1oversr;
} t_nw_pulsesamp;

void *nw_pulsesamp_new(t_symbol *snd);
void nw_pulsesamp_perform64zero(t_nw_pulsesamp *x, t_object *dsp64, double **ins, long numins, double **outs,long numouts, long vectorsize, long flags, void *userparam);
void nw_pulsesamp_perform64(t_nw_pulsesamp *x, t_object *dsp64, double **ins, long numins, double **outs,long numouts, long vectorsize, long flags, void *userparam);
void nw_pulsesamp_dsp64(t_nw_pulsesamp *x, t_object *dsp64, short *count, double samplerate, long maxvectorsize, long flags);
void nw_pulsesamp_setsnd(t_nw_pulsesamp *x, t_symbol *s);
void nw_pulsesamp_float(t_nw_pulsesamp *x, double f);
void nw_pulsesamp_int(t_nw_pulsesamp *x, long l);
void nw_pulsesamp_initGrain(t_nw_pulsesamp *x, float in_samp_inc, float in_gain, float in_start, float in_end);
void nw_pulsesamp_sndInterp(t_nw_pulsesamp *x, long l);
void nw_pulsesamp_reverse(t_nw_pulsesamp *x, long l);
void nw_pulsesamp_assist(t_nw_pulsesamp *x, t_object *b, long msg, long arg, char *s);
void nw_pulsesamp_getinfo(t_nw_pulsesamp *x);
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
    
    c = class_new(OBJECT_NAME, (method)nw_pulsesamp_new, (method)dsp_free,
                  (short)sizeof(t_nw_pulsesamp), 0L, A_SYM, 0);
    class_dspinit(c); // add standard functions to class
	
	/* bind method "nw_pulsesamp_setsnd" to the 'set' message */
	class_addmethod(c, (method)nw_pulsesamp_setsnd, "set", A_SYM, 0);
	
	/* bind method "nw_pulsesamp_float" to incoming floats */
	class_addmethod(c, (method)nw_pulsesamp_float, "float", A_FLOAT, 0);
	
	/* bind method "nw_pulsesamp_int" to incoming ints */
	class_addmethod(c, (method)nw_pulsesamp_int, "int", A_LONG, 0);
	
	/* bind method "nw_pulsesamp_reverse" to the direction message */
	class_addmethod(c, (method)nw_pulsesamp_reverse, "reverse", A_LONG, 0);
	
	/* bind method "nw_pulsesamp_sndInterp" to the interpolation message */
	class_addmethod(c, (method)nw_pulsesamp_sndInterp, "interpolation", A_LONG, 0);
	
	/* bind method "nw_pulsesamp_assist" to the assistance message */
	class_addmethod(c, (method)nw_pulsesamp_assist, "assist", A_CANT, 0);
	
	/* bind method "nw_pulsesamp_getinfo" to the getinfo message */
	class_addmethod(c, (method)nw_pulsesamp_getinfo, "getinfo", A_NOTHING, 0);
    
    /* bind method "nw_pulsesamp_dsp64" to the dsp64 message */
    class_addmethod(c, (method)nw_pulsesamp_dsp64, "dsp64", A_CANT, 0);
	
    class_register(CLASS_BOX, c); // register the class w max
    pulsesamp_class = c;
	
	/* needed for 'buffer~' work, checks for validity of buffer specified */
	ps_buffer = gensym("buffer~");
	
    #ifdef DEBUG
    
    #endif /* DEBUG */
    
    return 0;
}

/********************************************************************************
void *nw_pulsesamp_new(double initial_pos)

inputs:			*snd		-- name of buffer holding sound
description:	called for each new instance of object in the MAX environment;
		defines inlets and outlets; sets variables and buffers
returns:		nothing
********************************************************************************/
void *nw_pulsesamp_new(t_symbol *snd)
{
	t_nw_pulsesamp *x = (t_nw_pulsesamp *) object_alloc((t_class*) pulsesamp_class);
	dsp_setup((t_pxobject *)x, 5);					// five inlets
	outlet_new((t_pxobject *)x, "signal");			// overflow outlet
	outlet_new((t_pxobject *)x, "signal");          // sample count outlet
	outlet_new((t_pxobject *)x, "signal");			// signal ch2 outlet
	outlet_new((t_pxobject *)x, "signal");			// signal ch1 outlet
	
	/* set buffer names */
	x->snd_sym = snd;
	
	/* zero pointers */
	x->snd_buf_ptr = x->next_snd_buf_ptr = NULL;
	
	/* setup variables */
	x->grain_samp_inc = x->next_grain_samp_inc = 1.0;
	x->grain_gain = x->next_grain_gain = 1.0;
	x->grain_start = x->next_grain_start = 0.0;  // add 2005.10.10
	x->grain_end = x->next_grain_end = -1.0;	//add 2005.10.10
	x->snd_step_size = 1.0;
	x->curr_snd_pos = 0.0;
	x->last_pulse_in = 0.0;
	x->curr_count_samp = -1;
	
	/* set flags to defaults */
	x->snd_interp = INTERP_ON;
	x->grain_direction = x->next_grain_direction = FORWARD_GRAINS;
	
	x->x_obj.z_misc = Z_NO_INPLACE;
	
	/* return a pointer to the new object */
	return (x);
}


/********************************************************************************
void nw_pulsesamp_dsp64()

inputs:     x		-- pointer to this object
            dsp64		-- signal chain to which object belongs
            count	-- array detailing number of signals attached to each inlet
            samplerate -- number of samples per second
            maxvectorsize -- sample frames per vector of audio
            flags --
description:	called when 64 bit DSP call chain is built; adds object to signal flow
returns:		nothing
********************************************************************************/
void nw_pulsesamp_dsp64(t_nw_pulsesamp *x, t_object *dsp64, short *count, double samplerate,
                 long maxvectorsize, long flags)
{
    
    #ifdef DEBUG
        object_post((t_object*)x, "adding 64 bit perform method");
    #endif /* DEBUG */
    
    /* set buffers */
    nw_pulsesamp_setsnd(x, x->snd_sym);
    
    /* test inlets for signal data */
    x->grain_samp_inc_connected = count[1];
    x->grain_gain_connected = count[2];
    x->grain_start_connected = count[3];
    x->grain_end_connected = count[4];
    
    x->output_sr = samplerate;
    x->output_1oversr = 1.0 / x->output_sr;
    
    //set overflow status
    x->overflow_status = OVERFLOW_OFF;
    
    if (count[5] || count[6]) {	// if output 1 or 2 are connected..
        #ifdef DEBUG
            object_post((t_object*)x, "output is being computed");
        #endif /* DEBUG */
        dsp_add64(dsp64, (t_object*)x, (t_perfroutine64)nw_pulsesamp_perform64, 0, NULL);
    } else {
        #ifdef DEBUG
            object_post((t_object*)x, "no output computed");
        #endif /* DEBUG */
    }
    
}


/********************************************************************************
void *nw_pulsesamp_perform64zero()

inputs:	x		--
        dsp64   --
        ins     --
        numins  --
        outs    --
        numouts --
        vectorsize --
        flags   --
        userparam  --
description:	called at interrupt level to compute object's output at 64-bit,
    writes zeros to every outlet
returns:		nothing
********************************************************************************/
void nw_pulsesamp_perform64zero(t_nw_pulsesamp *x, t_object *dsp64, double **ins, long numins, double **outs,
                      long numouts, long vectorsize, long flags, void *userparam)
{
	for (long channel = 0; channel<numouts; ++channel) {
		for (long i = 0; i<vectorsize; ++i)
			outs[channel][i] = 0.0;
	}
}

/********************************************************************************
 void *nw_pulsesamp_perform64()
 
 inputs:	x		--
 dsp64   --
 ins     --
 numins  --
 outs    --
 numouts --
 vectorsize --
 flags   --
 userparam  --
 description:	called at interrupt level to compute object's output at 64-bit
 returns:		nothing
 ********************************************************************************/
void nw_pulsesamp_perform64(t_nw_pulsesamp *x, t_object *dsp64, double **ins, long numins, double **outs,
                                long numouts, long vectorsize, long flags, void *userparam)
{
    // local vars outlets and inlets
    double *in_pulse = ins[0];
    double *in_sample_increment = ins[1];
    double *in_gain = ins[2];
    double *in_start = ins[3];
    double *in_end = ins[4];
    double *out_signal = outs[0];
    double *out_signal2 = outs[1];
    double *out_sample_count = outs[2];
    double *out_overflow = outs[3];
    
    // local vars for snd buffer
    t_buffer_obj *snd_object;
    float *tab_s;
    float snd_out;
    float snd_out2;
    long size_s, chan_s;
    
    // local vars for object vars and while loop
    double index_s, index_s_start, index_s_end;
    double s_step_size, g_gain;
    float last_s, last_pulse;
    long count_samp;
    short interp_s, g_direction, of_status;
    long n, temp_index_int, temp_index_int_times_chan;
    double temp_index_frac;
    
    /* check to make sure buffers are loaded with proper file types*/
    if (x->x_obj.z_disabled)		// object is enabled
        goto out;
    if (x->snd_buf_ptr == NULL)     // buffer pointer is defined
        goto zero;
    
    // get snd buffer info
    snd_object = buffer_ref_getobject(x->snd_buf_ptr);
    tab_s = buffer_locksamples(snd_object);
    if (!tab_s)		// buffer samples were not accessible
        goto zero;
    size_s = buffer_getframecount(snd_object);
    chan_s = buffer_getchannelcount(snd_object);
    
    // get snd index info
    index_s_start = x->grain_start;
    index_s_end = x->grain_end;
    s_step_size = x->snd_step_size;
    
    // get grain options
    g_gain = x->grain_gain;
    interp_s = x->snd_interp;
    g_direction = x->grain_direction;
    
    // get history from last vector
    last_s = x->snd_last_out;
    index_s = x->curr_snd_pos;
    last_pulse = x->last_pulse_in;
    of_status = x->overflow_status;
    count_samp = x->curr_count_samp;
    
    n = vectorsize;
    while(n--)
    {
        // should we start reading sample segment ?
        if (count_samp == -1) { // if sample count is -1...
            if (last_pulse == 0.0 && *in_pulse == 1.0) { // if pulse begins...
                buffer_unlocksamples(snd_object);
                
                nw_pulsesamp_initGrain(x, *in_sample_increment, *in_gain, *in_start, *in_end);
                
                /* update local vars again */
                
                // get snd buffer info
                snd_object = buffer_ref_getobject(x->snd_buf_ptr);
                tab_s = buffer_locksamples(snd_object);
                if (!tab_s)	{	// buffer samples were not accessible
                    *out_signal = 0.0;
                    *out_signal2 = 0.0;
                    *out_overflow = 0.0;
                    *out_sample_count = (double)count_samp;
                    last_pulse = *in_pulse;
                    goto advance_pointers;
                }
                size_s = buffer_getframecount(snd_object);
                
                // get snd index info
                index_s_start = x->grain_start;
                index_s_end = x->grain_end;
                s_step_size = x->snd_step_size;
                
                // get grain options
                g_gain = x->grain_gain;
                interp_s = x->snd_interp;
                g_direction = x->grain_direction;
                
                // other history
                last_s = x->snd_last_out;
                index_s = x->curr_snd_pos;
                last_pulse = x->last_pulse_in;
                /*** of_status = x->overflow_status; ***/
                count_samp = x->curr_count_samp;
                
                // BUT this stays off until duty cycle ends
                of_status = OVERFLOW_OFF;
                
            } else { // if not...
                *out_signal = 0.0;
                *out_signal2 = 0.0;
                *out_overflow = 0.0;
                *out_sample_count = (double)count_samp;
                last_pulse = *in_pulse;
                goto advance_pointers;
            }
        }
        
        //pulse tracking for overflow
        if (!of_status) {
            if (last_pulse == 1.0 && *in_pulse == 0.0) { // if grain on & pulse ends...
                of_status = OVERFLOW_ON;	//start overflowing
            }
        }
        
        // advance snd index
        if (g_direction == FORWARD_GRAINS) {	// if forward...
            index_s += s_step_size;		// add to sound index
            
            /* check bounds of buffer index */
            if (index_s > index_s_end) {
                count_samp = -1;
                *out_signal = 0.0;
                *out_signal2 = 0.0;
                *out_overflow = 0.0;
                *out_sample_count = (double)count_samp;
                last_pulse = *in_pulse;
                #ifdef DEBUG
                    object_post((t_object*)x, "end of grain");
                #endif /* DEBUG */
                goto advance_pointers;
            }
            
        } else {	// if reverse...
            index_s -= s_step_size;		// subtract from sound index
            
            /* check bounds of buffer index */
            if (index_s < index_s_start) {
                count_samp = -1;
                *out_signal = 0.0;
                *out_signal2 = 0.0;
                *out_overflow = 0.0;
                *out_sample_count = (double)count_samp;
                last_pulse = *in_pulse;
                #ifdef DEBUG
                    object_post((t_object*)x, "end of grain");
                #endif /* DEBUG */
                goto advance_pointers;
            }
            
        }
        
        // if we made it here, then we will actually start counting
        count_samp++;
        
        // compute temporary vars for interpolation
        temp_index_int = (long)(index_s); // integer portion of index
        temp_index_frac = index_s - (double)temp_index_int; // fractional portion of index
        temp_index_int_times_chan = temp_index_int * chan_s;
        
        // get value from the snd buffer samples
        // if stereo, get values from each channel
        // if mono, get one value and copy to both outputs
        if (interp_s == INTERP_OFF) {
            snd_out = tab_s[temp_index_int_times_chan];
            snd_out2 = (chan_s == 2) ?
                tab_s[temp_index_int_times_chan + 1] :
                snd_out;
        } else {
            snd_out = mcLinearInterp(tab_s, temp_index_int_times_chan, temp_index_frac, size_s, chan_s);
            snd_out2 = (chan_s == 2) ?
                mcLinearInterp(tab_s, temp_index_int_times_chan + 1, temp_index_frac, size_s, chan_s) :
                snd_out;
        }
        
        // multiply snd_out by gain value
        *out_signal = snd_out * g_gain;
        *out_signal2 = snd_out2 * g_gain;
        
        if (of_status) {
            *out_overflow = *in_pulse;
        } else {
            *out_overflow = 0.0;
        }
        
        *out_sample_count = (double)count_samp;
        
        // update vars for last output
        last_pulse = *in_pulse;
        last_s = snd_out;
        
advance_pointers:
        // advance all pointers
        ++in_pulse, ++in_sample_increment, ++in_gain, ++in_start, ++in_end;
        ++out_signal, ++out_signal2, ++out_overflow, ++out_sample_count;
    }
    
    // update object history for next vector
    x->snd_last_out = last_s;
    x->curr_snd_pos = index_s;
    x->last_pulse_in = last_pulse;
    x->overflow_status = of_status;
    x->curr_count_samp = count_samp;
    
    buffer_unlocksamples(snd_object);
    return;

// alternate blank output
zero:
    n = vectorsize;
    while(n--)
    {
        *out_signal++ = 0.;
        *out_signal2++ = 0.;
        *out_overflow++ = -1.;
        *out_sample_count++ = -1.;
    }

out:
    return;
    
}

/********************************************************************************
void nw_pulsesamp_initGrain(t_nw_pulsesamp *x, float in_samp_inc, float in_gain)

inputs:			x					-- pointer to this object
				in_samp_inc			-- playback sample increment, corrected for sr
				in_gain				-- gain multiplier for sample
				in_start			-- where to start reading buffer, in ms
				in_end				-- where to stop reading buffer, in ms
description:	initializes grain vars; called from perform method when bang is 
		received
returns:		nothing 
********************************************************************************/
void nw_pulsesamp_initGrain(t_nw_pulsesamp *x, float in_samp_inc, float in_gain, 
	float in_start, float in_end)
{
	#ifdef DEBUG
		object_post((t_object*)x, "initializing grain");
	#endif /* DEBUG */
    
    t_buffer_obj	*snd_object;
	
	if (x->next_snd_buf_ptr != NULL) {
		x->snd_buf_ptr = x->next_snd_buf_ptr;
		x->next_snd_buf_ptr = NULL;
		
		#ifdef DEBUG
			object_post((t_object*)x, "buffer pointer updated");
		#endif /* DEBUG */
	}
	
	snd_object = buffer_ref_getobject(x->snd_buf_ptr);
	
	/* should input variables be at audio or control rate ? */
	
    x->grain_samp_inc = x->grain_samp_inc_connected ? in_samp_inc : x->next_grain_samp_inc;
    
    x->grain_gain = x->grain_gain_connected ? in_gain : x->next_grain_gain;
	
    x->grain_start = x->grain_start_connected ? in_start : x->next_grain_start;
    
    x->grain_end = x->grain_end_connected ? in_end : x->next_grain_end;
    
    /* compute dependent variables */
    
	// compute sound buffer step size per vector sample
	x->snd_step_size = x->grain_samp_inc * buffer_getsamplerate(snd_object) * x->output_1oversr;
    if (x->snd_step_size < 0.) x->snd_step_size *= -1.; // needs to be positive to prevent buffer overruns
	
    // update grain direction
    x->grain_direction = x->next_grain_direction;
	
	// convert start to samples
	x->grain_start = (long)((x->grain_start * buffer_getmillisamplerate(snd_object)) + 0.5);
	
	// convert end to samples
	x->grain_end = (long)((x->grain_end * buffer_getmillisamplerate(snd_object)) + 0.5);
	
	// test if end within bounds
	if (x->grain_end < 0. || x->grain_end > (double)(buffer_getframecount(snd_object))) x->grain_end =
        (double)(buffer_getframecount(snd_object));
	
    // test if start within bounds
	if (x->grain_start < 0. || x->grain_start > x->grain_end) x->grain_start = 0.;
	
	// set initial sound position based on direction
	if (x->grain_direction == FORWARD_GRAINS) {	// if forward...
		x->curr_snd_pos = x->grain_start - x->snd_step_size;
	} else {	// if reverse...
		x->curr_snd_pos = x->grain_end + x->snd_step_size;
	}
	
	// reset history
	x->snd_last_out = 0.0;
	x->curr_count_samp = -1;
	
	#ifdef DEBUG
		object_post((t_object*)x, "beginning of grain");
		object_post((t_object*)x, "samp_inc = %f samps", x->snd_step_size);
	#endif /* DEBUG */
}

/********************************************************************************
void nw_pulsesamp_setsnd(t_index *x, t_symbol *s)

inputs:			x		-- pointer to this object
				s		-- name of buffer to link
description:	links buffer holding the grain sound source 
returns:		nothing
********************************************************************************/
void nw_pulsesamp_setsnd(t_nw_pulsesamp *x, t_symbol *s)
{
	t_buffer_ref *b = buffer_ref_new((t_object*)x, s);;
	
	if (buffer_ref_exists(b)) {
        t_buffer_obj	*b_object = buffer_ref_getobject(b);
        
		if (buffer_getchannelcount(b_object) > 2) {
			object_error((t_object*)x, "buffer~ > %s < must be mono or stereo", s->s_name);
			x->next_snd_buf_ptr = NULL;		//added 2002.07.15
		} else {
			if (x->snd_buf_ptr == NULL) { // make current buffer
				x->snd_sym = s;
				x->snd_buf_ptr = b;
				x->snd_last_out = 0.0;
				
				#ifdef DEBUG
					object_post((t_object*)x, "current sound set to buffer~ > %s <", s->s_name);
				#endif /* DEBUG */
			} else { // defer to next buffer
				x->snd_sym = s;
				x->next_snd_buf_ptr = b;
				//x->snd_buf_length = b->b_frames;	//removed 2002.07.11
				//x->snd_last_out = 0.0;		//removed 2002.07.24
				
				#ifdef DEBUG
					object_post((t_object*)x, "next sound set to buffer~ > %s <", s->s_name);
				#endif /* DEBUG */
			}
		}
        
	} else {
		object_error((t_object*)x, "no buffer~ * %s * found", s->s_name);
		x->next_snd_buf_ptr = NULL;
	}
}

/********************************************************************************
void nw_pulsesamp_float(t_nw_pulsesamp *x, double f)

inputs:			x		-- pointer to our object
				f		-- value of float input
description:	handles floats sent to inlets; inlet 2 sets "next_grain_pos_start" 
		variable; inlet 3 sets "next_grain_length" variable; inlet 4 sets 
		"next_grain_pitch" variable; left inlet generates error message in max 
		window
returns:		nothing
********************************************************************************/
void nw_pulsesamp_float(t_nw_pulsesamp *x, double f)
{
	if (x->x_obj.z_in == 1) // if inlet 2
	{
		x->next_grain_samp_inc = f;
	}
	else if (x->x_obj.z_in == 2) // if inlet 3
	{
		x->next_grain_gain = f;
		
	}
	else if (x->x_obj.z_in == 3) // if inlet 4
	{
		x->next_grain_start = f;  // add 2005.10.10
	}
	else if (x->x_obj.z_in == 4) // if inlet 5
	{
		x->next_grain_end = f;    // add 2005.10.10
	}
	else
	{
		object_post((t_object*)x, "that inlet does not accept floats");
	}
}

/********************************************************************************
void nw_pulsesamp_int(t_nw_pulsesamp *x, long l)

inputs:			x		-- pointer to our object
				l		-- value of int input
description:	handles ints sent to inlets; inlet 2 sets "next_grain_pos_start" 
		variable; inlet 3 sets "next_grain_length" variable; inlet 4 sets 
		"next_grain_pitch" variable; left inlet generates error message in max 
		window
returns:		nothing
********************************************************************************/
void nw_pulsesamp_int(t_nw_pulsesamp *x, long l)
{
	if (x->x_obj.z_in == 1) // if inlet 2
	{
		x->next_grain_samp_inc = (double) l;
	}
	else if (x->x_obj.z_in == 2) // if inlet 3
	{
		x->next_grain_gain = (double) l;
	}
	else if (x->x_obj.z_in == 3) // if inlet 4
	{
		x->next_grain_start = (double) l;  // add 2005.10.10
	}
	else if (x->x_obj.z_in == 4) // if inlet 5
	{
		x->next_grain_end = (double) l;    // add 2005.10.10
	}
	else
	{
		object_post((t_object*)x, "that inlet does not accept ints");
	}
}

/********************************************************************************
void nw_pulsesamp_sndInterp(t_nw_pulsesamp *x, long l)

inputs:			x		-- pointer to our object
				l		-- flag value
description:	method called when "interpolation" message is received; allows user
		to define whether interpolation is used in pulling values from the sound
		buffer; default is on
returns:		nothing
********************************************************************************/
void nw_pulsesamp_sndInterp(t_nw_pulsesamp *x, long l)
{
	if (l == INTERP_OFF) {
		x->snd_interp = INTERP_OFF;
		#ifdef DEBUG
			object_post((t_object*)x, "interpolation is set to off");
		#endif // DEBUG //
	} else if (l == INTERP_ON) {
		x->snd_interp = INTERP_ON;
		#ifdef DEBUG
			object_post((t_object*)x, "interpolation is set to on");
		#endif // DEBUG //
	} else {
		object_error((t_object*)x, "interpolation message was not understood");
	}
}

/********************************************************************************
void nw_pulsesamp_reverse(t_nw_pulsesamp *x, long l)

inputs:			x		-- pointer to our object
				l		-- flag value
description:	method called when "reverse" message is received; allows user 
		to define whether sound is played forward or reverse; default is forward
returns:		nothing
********************************************************************************/
void nw_pulsesamp_reverse(t_nw_pulsesamp *x, long l)
{
	if (l == REVERSE_GRAINS) {
		x->next_grain_direction = REVERSE_GRAINS;
		#ifdef DEBUG
			object_post((t_object*)x, "reverse is set to on");
		#endif // DEBUG //
	} else if (l == FORWARD_GRAINS) {
		x->next_grain_direction = FORWARD_GRAINS;
		#ifdef DEBUG
			object_post((t_object*)x, "reverse is set to off");
		#endif // DEBUG //
	} else {
		object_error((t_object*)x, "reverse was not understood");
	}
	
}

/********************************************************************************
void nw_pulsesamp_assist(t_nw_pulsesamp *x, t_object *b, long msg, long arg, char *s)

inputs:			x		-- pointer to our object
				b		--
				msg		--
				arg		--
				s		--
description:	method called when "assist" message is received; allows inlets 
		and outlets to display assist messages as the mouse passes over them
returns:		nothing
********************************************************************************/
void nw_pulsesamp_assist(t_nw_pulsesamp *x, t_object *b, long msg, long arg, char *s)
{
	if (msg==ASSIST_INLET) {
		switch (arg) {
			case 0:
				strcpy(s, "(signal) pulse starts buffer segment");
				break;
			case 1:
				strcpy(s, "(signal/float) sample increment, 1.0 = unchanged");
				break;
			case 2:
				strcpy(s, "(signal/float) gain multiplier, 1.0 = unchanged");
				break;
			case 3:
				strcpy(s, "(signal/float) start in ms");
				break;
			case 4:
				strcpy(s, "(signal/float) end in ms");
				break;
		}
	} else if (msg==ASSIST_OUTLET) {
		switch (arg) {
			case 0:
				strcpy(s, "(signal) audio channel 1");
				break;
			case 1:
				strcpy(s, "(signal) audio channel 2");
				break;
			case 2:
				strcpy(s, "(signal) sample count");
				break;
			case 3:
				strcpy(s, "(signal) overflow");
				break;
		}
	}
	
	#ifdef DEBUG
		object_post((t_object*)x, "assist message displayed");
	#endif /* DEBUG */
}

/********************************************************************************
void nw_pulsesamp_getinfo(t_nw_pulsesamp *x)

inputs:			x		-- pointer to our object
				
description:	method called when "getinfo" message is received; displays info
		about object and lst update
returns:		nothing
********************************************************************************/
void nw_pulsesamp_getinfo(t_nw_pulsesamp *x)
{
	object_post((t_object*)x, "%s object by Nathan Wolek", OBJECT_NAME);
	object_post((t_object*)x, "Last updated on %s - www.nathanwolek.com", __DATE__);
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
	while (index_iP1 >= in_size * in_chans) index_iP1 -= in_size;
	
	// get samples
	sample1 = (double)in_array[index_i];
	sample2 = (double)in_array[index_iP1];
	
	//linear interp formula
	out = sample1 + index_frac * (sample2 - sample1);
	
	return out;
}