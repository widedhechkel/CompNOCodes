/*********************************************************************
 * NAME      : beamform.c
 * ABSTRACT  : Functions and utilities for beamforming an image.
 *
 *********************************************************************/

#include "../h/beamform.h" 
#include "../h/error.h"

#include <string.h>
#include <stdlib.h>

#ifndef NOTHREAD
#include <pthread.h>
#endif

/*********************************************************************
 * FUNCTION  : beamform_line_times(ftl, sys, time, rf_data, no_samples )
 * ABSTRACT  : beamform one line, which has multiple focal points in
 *             one line.  The number of output samples is equal to 
 *             the number of samples recorded by the individual elements
 * ARGUMENTS : ftl - Pointer to Focus Time Line
 *             sys - Pointer to System Paramaters
 *             time - The receive time of the first sample
 *             rf_data - 2D array with data. The number of columns is 
 *                       equal to the number of elements of the 
 *                       transducer, and the number of rows is equal to 
 *                       the number of samples.
 *             no_samples - The number of samples in one recorded, and
 *                          respectively beamformed scan line.
 * RETURNS  : The function allocates memory for the beamformed scan 
 *            line and returns the pointer to it.
 *********************************************************************/

double* beamform_line_times(TFocusTimeLine *ftl, TSysParams* sys,
			    double time, double **rf_data, ui32 no_samples) 
{
  double *bf_line;
  ui32 os;         /*  Index of output sample       */
  ui32 o_abs_s;    /*  Output absolut index         */
  ui32 is1;        /*  Index of input sample1       */
  si32 *d;         /*  Pointer to the delays        */
  double *a;       /*  Coefficient for linear interpolation */
  double A;        /*  One apodization value        */
  ui32 id;         /*  Index of delay               */
  ui32 ind;        /*  Index of next delay          */
  ui32 no_elements;/*  Number of XDC elements       */
  ui32 ic;         /*  Index of channel             */

  PFUNC;
  
  bf_line = (double *) malloc(no_samples * sizeof(double));
  o_abs_s = (ui32)floor(time * sys->fs);
  id = 0;
  ind = id + 1;
  no_elements = ftl->xdc->no_elements;
  /*
   *   Find the first useful set of delays for beamforming. 
   *   This is the set with the biggest starting time
   *   which  is less than 'time'
   */  
  while( (ftl->delay[ind].time < o_abs_s)) {  ind ++; id ++;}

  d = ftl->delay[id].d;
  a = ftl->delay[id].a;
  /*
   *   Beamform the output line one sample at a time. 
   */    
  for (os = 0; os < no_samples; o_abs_s++, os ++)
    {
      bf_line[os] = 0;
      if (o_abs_s > ftl->delay[ind].time) 
	{
	  ind ++; id ++;
	  d = ftl->delay[id].d;
	  a = ftl->delay[id].a;
	}
      for (ic = 0; ic < no_elements; ic ++ )
	{  
          is1  = os - d[ic];
	  if (is1 == 0) {
	    bf_line[os] += rf_data[ic][is1];
	  } 
	  else if (is1 < no_samples-1)
	    {
	      A = a[ic];
	      bf_line[os] +=  (double)rf_data[ic][is1] * (1-A)
		+ (double)rf_data[ic][is1-1] * A;
	    }
	}  
    }
  return bf_line;
}


/*********************************************************************
 * FUNCTION : apodize_fix(atl, rf_data, no_samples, no_channels)
 * ABSTRACT : Apply apodization on the rf_data. The input array gets 
 *            modified.
 * ARGUMENTS: atl - Apodization time line
 *            rf_data - 2D array with RF data to be apodized
 *            no_samples - No of samples in one line.
 *            no_channels - No of channels used to record the data.
 * RETURNS  : Pointer to 'rf_data'
 **********************************************************************/
double** apodize_fix(TApoTimeLine *atl, double **rf_data,
		     ui32 no_samples, ui32 no_channels)
{
  double *a;           /*  pointer to the apodization coefficients   */
  ui32 is;             /*  Index of sample                           */
  ui32 ic;             /*  Index of channel                          */
    
  
  if(atl->no_times != 1){
    printf("\007 apodize_fix :\n");
    printf("Error : there are more than one apodization zones\n");
    return rf_data;
  }
  
  a = atl->a->a;
  for(is = 0; is < no_samples; is ++){
    for(ic = 0; ic < no_channels; ic ++)
      rf_data[is][ic]*=a[ic];
  }
  return rf_data;
}


/*********************************************************************
 * FUNCTION : beamform_apo_line_times(ftl, atl, sysm time, rf_data,
 *                                    no_samples)
 * ABSTRACT : Beamforms and apodizes a line.
 * ARGUMENTS: ftl - Focus Time Line
 *            atl - Apodization Time Line
 *            sys - System parameters
 *            time - Time of the reception of the first sample
 *            rf_data - 2D array with RF data.
 *            no_samples - Number of samples in one scan line.
 * RETURNS  : The function allocates memory for the beamformed RF line
 *            and returns a pointer to it.
 * 
 *********************************************************************/

double* beamform_apo_line_times(TFocusTimeLine *ftl, TApoTimeLine* atl,
				TSysParams* sys, double time, 
				double **rf_data, ui32 no_samples) 
{
  double *bf_line;
  ui32 os;                /*  Index of output sample       */
  ui32 o_abs_s;           /*  Output absolut index         */
  ui32 is1;               /*  Index of input sample1       */
  si32 *d;                /*  Pointer to the delays        */
  double *a;              /*  Apodization array            */
  double A;               /*  One apodization value        */
  ui32 id;                /*  Index of delay               */
  ui32 ind;               /*  Index of next delay          */
  ui32 no_elements;       /*  No of elements in XDC        */
  ui32 ic;                /*  Index of channel             */
  ui32 ia;                /*  Index of apodization         */
  ui32 ina;               /*  Index of next apodization    */
  double* apo;            /*  Pointer to the apodization   */

  
  if (atl->no_times == 0){
    return beamform_line_times(ftl,sys,time,rf_data,no_samples);
  }
  
  bf_line = (double *) malloc(no_samples * sizeof(double));
  o_abs_s = (ui32)floor(time * sys->fs);
  id = 0; ind = 1; 
  ia = 0; ina = 1;
  
  no_elements = ftl->xdc->no_elements;
  
  /*
   *   Find the first useful set of delays for beamforming. 
   *   This is the set with the biggest starting time
   *   which  is less than 'time'
   */  
  while( ftl->delay[ind].time < o_abs_s ) {  ind ++; id ++;}
  while( atl->a[ina].time < o_abs_s) {ina ++; ia ++;}
  
  d = ftl->delay[id].d;
  a = ftl->delay[id].a;
  apo = atl->a[ia].a;
  
 
  
  /*
   *   Beamform the output line one sample at a time. 
   */    
  no_samples--;
  bf_line[no_samples] = 0;  
  for (os = 0; os < no_samples; o_abs_s++, os ++){  
    bf_line[os] = 0;
    if (o_abs_s > ftl->delay[ind].time) 
      {
        ind ++; id ++;
        d = ftl->delay[id].d;
        a = ftl->delay[id].a;
      }


    if (o_abs_s > atl->a[ina].time) 
      {
        ina ++; ia ++;
        apo = atl->a[ia].a;
      }

    for (ic = 0; ic < no_elements; ic ++ ){  
      is1  = os - d[ic];
      if ((is1-1) < no_samples ){
	double d;
	A = a[ic];
	d =  (double)rf_data[ic][is1] * (1-A)
	  + (double)rf_data[ic][is1-1] * A;           
	bf_line[os] += d*apo[ic];
      }
    }  
  }
  return bf_line;
}



/**********************************************************************
 * FUNCTION : beamform_apo_line_dynamic
 * ABSTRACT : Dynamically focus and apodize a scan line
 **********************************************************************/
double* beamform_apo_line_dynamic(TFocusTimeLine *ftl, TApoTimeLine* atl,
				  TSysParams* sys, double time,  double **rf_data, ui32 no_samples)
{

  double *bf_line;     /* The beamformed line                          */
  TTransducer* xdc;    /* Pointer to the transducer used to calc delays*/
  TPoint3D p;          /* Current focal point                          */
  double dX;           /* Increments of X, Y, Z per sample             */
  double dY;
  double dZ;
  double dR;
  
  double sample_index; /* The true value of the input index            */
  ui32 o_abs_s;        /* Absolute output index                        */
  ui32 os;             /* Output index for bf_line                     */
  ui32 is1;            /*is1, is2 - Input indeces of the used  samples */
  ui32 ia;             /* Index of the currently used apodization      */
  ui32 ina;            /* Index of the next apodization value          */
  ui32 ic;             /* Index of channel                             */
  double A;            /* Coefficient for linear interpolation         */
  
  double *apo;         /* Array with the current apodization values    */



  
  bf_line = (double*)malloc(no_samples*sizeof(double));
  xdc = ftl->xdc;
  
  dR = sys->c / sys->fs / 2;
  dX = tan(ftl->dir_xz);
  dY = tan(ftl->dir_yz);
  
  dZ= dR/sqrt(1+dX*dX+dY*dY); /* dZ=sqrt(1+tan(dir_xz)^2+tan(dir_yz)^2)*/
  dX*= dZ;                    /* dX = tan(dir_xz) * dZ                 */
  dY*= dZ;                    /* dY = tan(dir_yz) * dZ                 */
  
  o_abs_s = (ui32)floor(time * sys->fs);    /* time => sample index    */

  p.x = ftl->center.x + dX*o_abs_s;
  p.y = ftl->center.y + dY*o_abs_s;
  p.z = ftl->center.z + dZ*o_abs_s;
  

  if (atl->no_times==0){
    printf("\007 For the time being the dynamic focusing is ");
    printf("performed only on lines for which apodization is ");
    printf("specified.\n");
    return NULL;
  }
  
  
  ia = 0; ina = 1;
  while( atl->a[ina].time < o_abs_s) {ina ++; ia ++;}
  apo = atl->a[ia].a;
  
  no_samples--;
  bf_line[no_samples] = 0;
  for (os = 0; os < no_samples; o_abs_s++, os ++){
    if (o_abs_s > atl->a[ina].time) {
      ina ++; ia ++;
      apo = atl->a[ia].a;
    }

    bf_line[os] = 0;
     
    for(ic = 0; ic < xdc->no_elements; ic ++){
      sample_index = distance(&ftl->center, &p)*sys->fs;
      sample_index -= distance(xdc->c+ic, &p)*sys->fs;
      sample_index = os - (sample_index / sys->c);
      is1 = (ui32)floor(sample_index);
      if (is1 < no_samples-1){
	A = sample_index - is1;
	bf_line[os] += apo[ic]*(rf_data[ic][is1]*(1-A)
				+ rf_data[ic][is1+1]*A);
      }
    }

    p.x+=dX;
    p.y+=dY;
    p.z+=dZ;     
  }
  return bf_line;
}

/**********************************************************************
 * FUNCTION : beamform_line_dynamic
 * ABSTRACT : Dynamically focus  a scan line
 **********************************************************************/
double* beamform_line_dynamic(TFocusTimeLine *ftl, 
			      TSysParams* sys, double time,  double **rf_data, ui32 no_samples)
{

  double *bf_line;     /* The beamformed line                          */
  TTransducer* xdc;    /* Pointer to the transducer used to calc delays*/
  TPoint3D p;          /* Current focal point                          */
  double dX;           /* Increments of X, Y, Z per sample             */
  double dY;
  double dZ;
  double dR;
  
  double sample_index; /* The true value of the input index            */
  ui32 o_abs_s;        /* Absolute output index                        */
  ui32 os;             /* Output index for bf_line                     */
  ui32 is1;            /*is1, is2 - Input indeces of the used  samples */
  ui32 ic;             /* Index of channel                             */
  double A;            /* Coefficient for linear interpolation         */
  
  
  bf_line = (double*)malloc(no_samples*sizeof(double));
  xdc = ftl->xdc;
  
  dR = sys->c / sys->fs / 2;
  dX = tan(ftl->dir_xz);
  dY = tan(ftl->dir_yz);
  
  dZ= dR/sqrt(1+dX*dX+dY*dY); /* dZ=sqrt(1+tan(dir_xz)^2+tan(dir_yz)^2)*/
  dX*= dZ;                    /* dX = tan(dir_xz) * dZ                 */
  dY*= dZ;                    /* dY = tan(dir_yz) * dZ                 */
  
  o_abs_s = (ui32)floor(time * sys->fs);    /* time => sample index    */

  p.x = ftl->center.x + dX*o_abs_s;
  p.y = ftl->center.y + dY*o_abs_s;
  p.z = ftl->center.z + dZ*o_abs_s;
  
  
  no_samples--;
  bf_line[no_samples] = 0;
  for (os = 0; os < no_samples; o_abs_s++, os ++){

    bf_line[os] = 0;
     
    for(ic = 0; ic < xdc->no_elements; ic ++){
      sample_index = distance(&ftl->center, &p)*sys->fs;
      sample_index -= distance(xdc->c+ic, &p)*sys->fs;
      sample_index = os - (sample_index / sys->c);
      is1 = (ui32)floor(sample_index);
      if (is1 < no_samples-1){
	A = sample_index - is1;
	bf_line[os] += (rf_data[ic][is1]*(1-A)
			+ rf_data[ic][is1+1]*A);
      }
    }

    p.x+=dX;
    p.y+=dY;
    p.z+=dZ;     
  }
  return bf_line;
}





/**********************************************************************
 * FUNCTION : beamform_apo_line_dynamic_sta
 * ABSTRACT : Dynamically focus and apodize a scan line
 **********************************************************************/
double* beamform_apo_line_dynamic_sta(TFocusTimeLine *ftl, TApoTimeLine* atl,
				      TSysParams* sys, double time,  double **rf_data, ui32 no_samples, TPoint3D *xmt)
{

  double *bf_line;     /* The beamformed line                          */
  TTransducer* xdc;    /* Pointer to the transducer used to calc delays*/
  TPoint3D p;          /* Current focal point                          */
  double dX;           /* Increments of X, Y, Z per sample             */
  double dY;
  double dZ;
  double dR;
  
  double sample_index; /* The true value of the input index            */
  ui32 o_abs_s;        /* Absolute output index                        */
  ui32 os;             /* Output index for bf_line                     */
  ui32 is1;            /*is1, is2 - Input indeces of the used  samples */
  ui32 ia;             /* Index of the currently used apodization      */
  ui32 ina;            /* Index of the next apodization value          */
  ui32 ic;             /* Index of channel                             */
  double A;            /* Coefficient for linear interpolation         */
  
  double *apo;         /* Array with the current apodization values    */
  double scaler;  
  double sample_base_index;
  double time_sample;
 
	
  PFUNC;
  
  bf_line = (double*)malloc(no_samples*sizeof(double));
  xdc = ftl->xdc;
  
  /* Radial distance per sample */
  dR = sys->c / sys->fs / 2;
  /* Temporary values of dX and dY*/
  dX = tan(ftl->dir_xz);
  dY = tan(ftl->dir_yz);
  
  /* Shifts per sample */
  dZ= dR/sqrt(1+dX*dX+dY*dY); /* dZ=sqrt(1+tan(dir_xz)^2+tan(dir_yz)^2)*/
  dX*= dZ;                    /* dX = tan(dir_xz) * dZ                 */
  dY*= dZ;                    /* dY = tan(dir_yz) * dZ                 */
  
  /* Finding offset to first sample (defined by time). */
  o_abs_s = (ui32)floor(time * sys->fs);    /* time => sample index    */

  /* Set p to start of first sample. */
  p.x = ftl->center.x + dX*o_abs_s;
  p.y = ftl->center.y + dY*o_abs_s;
  p.z = ftl->center.z + dZ*o_abs_s;
  
  /* Apodization is always set when this function is used. */
  if (atl->no_times==0){
    printf("\007 For the time being the dynamic focusing is ");
    printf("performed only on lines for which apodization is ");
    printf("specified.\n");
    return NULL;
  }
  
  /* Seems like apodization time has to be in samples, not documented. */
  ia = 0; ina = 1;
  while( atl->a[ina].time < o_abs_s) {ina ++; ia ++;}
  apo = atl->a[ia].a;
  
  /* Only want to iterate until 2nd last sample (last is always 0) */
  no_samples--;
  
  scaler = sys->fs / sys->c;  /* Scaler converts from distance to samples */
  time_sample = time * sys->fs; /* Start of data in samples. */
  
  bf_line[no_samples] = 0;
  sample_base_index = 0;
  
  for (os = 0; os < no_samples; o_abs_s++, os ++){
    double d;

    /* Advance the apodization if necessary */
    if (o_abs_s > atl->a[ina].time) {
      ina ++; ia ++;
      apo = atl->a[ia].a;
    }

    /* Find number of samples (along beam-line) from transmit location and
       offset to start of data (i.e. initial travel). */
    sample_base_index = distance(xmt, &p) * scaler - time_sample;
    d = 0;  
	  
    for(ic = 0; ic < xdc->no_elements; ic ++){
      /* Find number of samples between current element and current
	 point in samples (i.e. travel back), and add to previous path. */
      sample_index = distance(xdc->c+ic, &p)*scaler  + sample_base_index;
      is1 = (ui32)floor(sample_index);

      /* Perform weighted averaging for current sample. */
      if (is1 < no_samples-1){
	A = sample_index - is1;
	d += apo[ic]*(rf_data[ic][is1]*(1-A)
		      + rf_data[ic][is1+1]*A);
      }
    }
    
    /* Store data, move to next point. */
    bf_line[os] = d;

    p.x+=dX;
    p.y+=dY;
    p.z+=dZ;
  }
  return bf_line;
}



/**********************************************************************
 * FUNCTION : beamform_line_dynamic_sta
 * ABSTRACT : Dynamically focus  a scan line
 **********************************************************************/
double* beamform_line_dynamic_sta(TFocusTimeLine *ftl, 
				  TSysParams* sys, double time,  double **rf_data, ui32 no_samples, TPoint3D* xmt)
{

  double *bf_line;     /* The beamformed line                          */
  TTransducer* xdc;    /* Pointer to the transducer used to calc delays*/
  TPoint3D p;          /* Current focal point                          */
  double dX;           /* Increments of X, Y, Z per sample             */
  double dY;
  double dZ;
  double dR;
  
  double sample_index; /* The true value of the input index            */
  ui32 os;             /* Output index for bf_line                     */
  ui32 is1;            /*is1, is2 - Input indeces of the used  samples */
  ui32 ic;             /* Index of channel                             */
  double A;            /* Coefficient for linear interpolation         */
  double scaler;  
  double sample_base_index;
  double time_sample;
  
  PFUNC
  
    bf_line = (double*)malloc(no_samples*sizeof(double));
  xdc = ftl->xdc;
  
  dR = sys->c / sys->fs / 2;
  dX = tan(ftl->dir_xz);
  dY = tan(ftl->dir_yz);
  
  dZ= dR/sqrt(1+dX*dX+dY*dY); /* dZ=sqrt(1+tan(dir_xz)^2+tan(dir_yz)^2)*/
  dX*= dZ;                    /* dX = tan(dir_xz) * dZ                 */
  dY*= dZ;                    /* dY = tan(dir_yz) * dZ                 */
  

  time_sample = time * sys->fs;
 
  
  p.x = ftl->center.x + time_sample*dX;
  p.y = ftl->center.y + time_sample*dY;
  p.z = ftl->center.z + time_sample*dZ;
  
  
  no_samples--;
  bf_line[no_samples] = 0;
  scaler = sys->fs / sys->c;
  
  
  for (os = 0; os < no_samples; os ++){
    double d;
     
    d = 0;
    sample_base_index = distance(xmt, &p) * scaler - time_sample;
	  
    for(ic = 0; ic < xdc->no_elements; ic ++){
      sample_index = distance(xdc->c+ic, &p) * scaler + sample_base_index;
      is1 = (ui32)floor(sample_index);
      if (is1 < no_samples-1){
	A = sample_index - is1;
	bf_line[os] += (rf_data[ic][is1]*(1-A)
			+ rf_data[ic][is1+1]*A);
      }
    }
    bf_line[os] = d;
    p.x+=dX;
    p.y+=dY;
    p.z+=dZ;     
  }
  return bf_line;
}


/**********************************************************************
 * FUNCTION : beamform_line_pixels
 * ABSTRACT : Beamform a line using pixel-based focusing.
 **********************************************************************/
double* beamform_line_pixels(TFocusTimeLine *ftl, TSysParams* sys,
			     double time,  double **rf_data, ui32 no_samples
			     ,ui32 element_no)
{

  double *bf_line;     /* The beamformed line                          */
  TTransducer* xdc;    /* Pointer to the transducer used to calc delays*/
  
  double A;            /* Coefficient for linear interpolation         */
  double xmt_index=0;    /* Index of the sample connected with the transmit */
  double sample_index=0;
  double start_index;
  TPoint3D *p;        /* Focal point  */
  ui32 is1;           /* Input sample */
  ui32 is2;           /* Input sample  */
  ui32 os;            /* Output sample */
  ui32 ic;            /* Index of channel */
  int flag; 
  
  
  PFUNC;
    if (ftl->no_times < 1){
      printf("beamform_line_pixels: \007 \n");
      printf("Error: no focal points are specified.\n");
      assert(ftl->no_times>1);
    }
  if (ftl->pixels == NULL){
    printf("beamform_line_pixels: \007 \n");
    printf("Error: NULL pointer to the pixels.");
    assert(ftl->pixels);
  }
  printf("beamform_pixels:\n");
  bf_line = (double*)malloc(ftl->no_times*sizeof(double));
  
  start_index = time * sys->fs;
  xdc = ftl->xdc;
  
  flag = element_no >= xdc->no_elements;
    
  for (os = 0; os < ftl->no_times;  os ++){
    bf_line[os] = 0;
    p = ftl->pixels + os;
      
    if (element_no < xdc->no_elements){
      xmt_index = distance(xdc->c+element_no, p)*sys->fs;
      xmt_index =  (xmt_index / sys->c);
    }

    for(ic = 0; ic < xdc->no_elements; ic ++){
      sample_index =  distance(xdc->c+ic, p)*sys->fs;
      if (flag)
	sample_index = 2*sample_index / sys->c - start_index;
      else
	sample_index =  (sample_index / sys->c) - start_index;
  
      sample_index += xmt_index;
        
      is1 = (ui32)floor(sample_index);
      is2 = is1 - 1;
      if (is2 < no_samples && is1 < no_samples){
	A = sample_index - is1;
	bf_line[os] += (rf_data[ic][is1]*(1-A)
			+ rf_data[ic][is2]*A);
      }
    }
  }
  return bf_line;
}


/**********************************************************************
 * FUNCTION : beamform_apo_line_pixels
 * ABSTRACT : Beamform a line using pixel-based focusing.
 **********************************************************************/
double* beamform_apo_line_pixels(TFocusTimeLine *ftl, TApoTimeLine* atl, TSysParams* sys,
				 double time,  double **rf_data, ui32 no_samples,
				 ui32 element_no)
{

  double *bf_line;     /* The beamformed line                          */
  TTransducer* xdc;    /* Pointer to the transducer used to calc delays*/
  
  double A;            /* Coefficient for linear interpolation         */
  
  double sample_index=0;
  double start_index=0;
  double xmt_index = 0;
  TPoint3D *p;        /* Focal point  */
  ui32 ia, ina;       /* Index of apodization value and next apodization value */
  ui32 is1;           /* Input sample */
  ui32 is2;           /* Input sample  */
  ui32 os;            /* Output sample */
  ui32 ic;            /* Index of channel */
  double apo=1;         /* The apodization value to apply  */
  int flag;  
  
  if (ftl->no_times < 1){
    printf("beamform_apo_line_pixels: \007 \n");
    printf("Error: no focal points are specified.\n");
    assert(ftl->no_times>1);
  }
  if (ftl->pixels == NULL){
    printf("beamform_apo_line_pixels: \007 \n");
    printf("Error: NULL pointer to the pixels.");
    assert(ftl->pixels);
  }
  
  bf_line = (double*)malloc(ftl->no_times*sizeof(double));
  
  start_index = time * sys->fs;
  
  xdc = ftl->xdc;
  flag =element_no >= xdc->no_elements ;
  for (os = 0; os < ftl->no_times;  os ++){
    bf_line[os] = 0;
    p = ftl->pixels + os;
    if (element_no < xdc->no_elements){
      xmt_index = distance(xdc->c+element_no, p)*sys->fs;
      xmt_index =  (xmt_index / sys->c);
    }

    for(ic = 0; ic < xdc->no_elements; ic ++){
      sample_index = distance(xdc->c+ic, p)*sys->fs;
      if (flag)
	sample_index = 2*sample_index / sys->c - start_index;
      else
	sample_index =  (sample_index / sys->c) - start_index;
      ia = 0; ina = 1;
      while( atl->a[ina].time < sample_index) {ina ++; ia ++;}
      apo = atl->a[ia].a[ic];
      sample_index += xmt_index;
      is1 = (ui32)floor(sample_index);
      is2 = is1 + 1;
      if (is2 < no_samples && is1 < no_samples){
	A = sample_index - is1;
	bf_line[os] += apo*(rf_data[ic][is1]*(1-A)
                            + rf_data[ic][is2]*A);
      }
    }
  }
  return bf_line;
}





/*********************************************************************
 * FUNCTION : sum_lines_time
 * ABSTRACT : Sum 2 already beamformed lines into a new one.
 *********************************************************************/
double* sum_lines_time(TFocusTimeLine *ftl, TApoTimeLine* atl, TSysParams* sys, 
		       double* rf_line1, ui32 element1, 
		       double* rf_line2, ui32 element2,
		       double time,      ui32 no_samples)
{
  double* sum_line;
  double d1, A1;     /* Delay and weighting coefficient */
  double d2, A2;     /* Delay and weighting coefficient */
  double apo1;       /* Apodization value               */
  double apo2;
  ui32 id, ind;      /* Index of applied delay          */
  ui32 ia, ina;      /* Index of apodization            */
  ui32 os;           /* Index of output sample          */
  ui32 is1, is2;     /* Indexes of the input samples    */
  ui32 o_abs_s;      /* Absolute index of output sample */
   
   
  /* Filter a little bit the input parameters           */
  if (atl->no_times == 0){
    printf("sum_lines_time:\nError: ");
    printf("There must be at least one apodization \n");
    printf("\tNext time call 'bft_apodization'\n");
    abort();
  }
   
  if (ftl->no_times == 0){
    printf("sum_lines_time:\n Error: ");
    printf("There must be at least one focus time \n");
    printf("\tNext time call 'bft_focust_times' or 'bft_focus'\n");
    abort();
  }

  if (element1 > ftl->xdc->no_elements || element2 > ftl->xdc->no_elements){
    printf("sum_lines_time:\n");
    printf("Either the numer of element 1 or element 2 exceeds the number ");
    printf("of elements of the associated transducer\n");
    abort();
  }
   
   
  /* Allocate the memory for the output line and check  */
  sum_line = (double*)calloc(no_samples, sizeof(double));
  assert(sum_line);
   
  /* What would the sample index be, if time was =0     */  
  o_abs_s = time*sys->fs;
   
  /* Find the first delay and apodization to apply      */
  id = 0; ind = 1;
  while( ftl->delay[ind].time < o_abs_s) {ind ++; id ++;}
  d1 = ftl->delay[id].d[element1]; A1 = ftl->delay[id].a[element1];
  d2 = ftl->delay[id].d[element2]; A2 = ftl->delay[id].a[element1];
   
  ia = 0; ina = 1;
  while( atl->a[ina].time < o_abs_s) {ina ++; ia ++;}
  apo1 = atl->a[ia].a[element1];
  apo2 = atl->a[ia].a[element2];
   
  /* Beamform the line - one sample at a time           */
  no_samples --;
   
  for (os = 0; os < no_samples; os ++, o_abs_s++){
    if (o_abs_s > ftl->delay[ind].time){
      ind ++; id ++;
      d1= ftl->delay[id].d[element1]; A1= ftl->delay[id].a[element1];
      d2= ftl->delay[id].d[element2]; A2= ftl->delay[id].a[element2];
    }

    if (o_abs_s > atl->a[ina].time){
      ina ++; ia ++;
      apo1 = atl->a[ia].a[element1];
      apo2 = atl->a[ia].a[element2];
    }
      
    is1 = os - d1; is2 = os - d2;
      
    if((is1-1)<no_samples)
      sum_line[os]+= apo1*(rf_line1[is1]*(1-A1) + rf_line1[is1-1]*A1);

    if((is2-1)<no_samples)
      sum_line[os]+= apo2*(rf_line2[is2]*(1-A2) + rf_line2[is2-1]*A2); 
  }
   
  return sum_line;     
}


/*********************************************************************
 * FUNCTION : sum_images
 * ABSTRACT : Sum to low-resoltuion images in one high-resolution image
 *********************************************************************/
double **sum_images(TFocusLineCollection *flc, TApoLineCollection *alc,
                    TSysParams* sys,
                    double **rf1, ui32 element1,
                    double **rf2, ui32 element2, 
                    double time, ui32 no_samples)


{
  double **sum_lines;
  ui32 line_no;
  /*
   *   Filter the input parameters for wrong settings
   */
  if (flc->no_focus_time_lines != alc->no_apo_time_lines){
    printf("\007 sum_images:\n");
    printf("Error : the number of apodization lines and the number of ");
    printf("focus lines must be the same \n");
    return NULL;
  }
  
  if (flc->no_focus_time_lines == 0){
    printf("\007 sum_images:\n");
    printf("Error : the number of defined lines is 0\n");
    return NULL;
  }
  
   
  sum_lines = (double**)calloc(flc->no_focus_time_lines,sizeof(double*));
  assert(sum_lines);
  for (line_no = 0; line_no < flc->no_focus_time_lines; line_no ++){
    sum_lines[line_no] = sum_lines_time(flc->ftl+line_no, alc->atl+line_no, sys, 
					rf1[line_no], element1, rf2[line_no], element2,
					time, no_samples);
  }
  return sum_lines;
}

/*Thread helper functions for beamforming image at once.*/
void *beamform_thread_apo(void *param) {
  BFT_ThreadData *info = (BFT_ThreadData *)param;
  if (info->flc->ftl[info->i].dynamic == TRUE){
    if (info->elem!=NULL)
       *info->line = beamform_apo_line_dynamic_sta(info->flc->ftl+info->i,info->alc->atl+info->i,info->sys,info->time,info->rf_data,info->no_samples,info->elem);
    else
      *info->line = beamform_apo_line_dynamic(info->flc->ftl+info->i,info->alc->atl+info->i,info->sys,info->time,info->rf_data,info->no_samples);
  }else if(info->flc->ftl[info->i].pixel == TRUE){
    *info->line = beamform_apo_line_pixels(info->flc->ftl+info->i,info->alc->atl+info->i,info->sys,info->time,info->rf_data,info->no_samples,-1);
  }else{
    *info->line = beamform_apo_line_times(info->flc->ftl+info->i,info->alc->atl+info->i,info->sys,info->time,info->rf_data,info->no_samples);
  }
}


void *beamform_thread_noapo(void *param) {
  BFT_ThreadData *info = (BFT_ThreadData *)param;
  if (info->flc->ftl[info->i].dynamic == TRUE){
    if (info->elem!=NULL)
       *info->line = beamform_line_dynamic_sta(info->flc->ftl+info->i,info->sys,info->time,info->rf_data,info->no_samples,info->elem);
    else
      *info->line = beamform_line_dynamic(info->flc->ftl+info->i,info->sys,info->time,info->rf_data,info->no_samples);
  }else if(info->flc->ftl[info->i].pixel == TRUE){
    *info->line = beamform_line_pixels(info->flc->ftl+info->i,info->sys,info->time,info->rf_data,info->no_samples,-1);
  }else{
    *info->line = beamform_line_times(info->flc->ftl+info->i,info->sys,info->time,info->rf_data,info->no_samples);
  }
}

/*********************************************************************
 * FUNCTION  : beamform_image()
 * ABSTRACT  : beamforms a whole image
 *
 *********************************************************************/
double** beamform_image(TFocusLineCollection *flc, TApoLineCollection* alc,
			TSysParams* sys, double time, double **rf_data, ui32 no_samples,
			ui32 element_no, TPoint3D *xmt)
{
  double **bf_lines;      /* The collection of beamformed lines         */
  ui32 max_no_apo_times=0;
  ui32 i;
  TPoint3D* elem;
#ifndef NOTHREAD
  pthread_t *bf_threads;
#endif
  BFT_ThreadData *bf_thread_info;

  PFUNC
    /*
     *   Filter the input parameters for wrong settings
     */
    if (flc->no_focus_time_lines != alc->no_apo_time_lines){
      printf("\007 beamform_image:\n");
      printf("Error : the number of apodization lines and the number of ");
      printf("focus lines must be the same \n");
      return NULL;
    }
  if (flc->no_focus_time_lines == 0){
    printf("\007 beamform_image:\n");
    printf("Error : the number of defined lines is 0\n");
    return NULL;
  }
  
  bf_lines = (double**)calloc(flc->no_focus_time_lines,sizeof(double*));
  if (bf_lines == NULL){
    printf("\007 beamform_image:\n");
    printf("Error : cannot allocate memory for the output image\n");
    return NULL;
  }
  
  
  elem = xmt;
  

  /*
   *   Take decision which beamforming routine will be used
   */
  if (flc->no_focus_time_lines == 1){
    if (element_no < 64000 && elem==NULL) {
      elem = flc->ftl->xdc->c+element_no;	
    }
    if( flc->ftl->dynamic == TRUE){
      if (alc->atl->no_times > 0)
	if (elem!=NULL)
	  bf_lines[0] = beamform_apo_line_dynamic_sta(flc->ftl,alc->atl,sys,time,rf_data,no_samples, elem);
	else
	  bf_lines[0] = beamform_apo_line_dynamic(flc->ftl,alc->atl,sys,time,rf_data,no_samples);
      else
	if (elem != NULL)
	  bf_lines[0] = beamform_line_dynamic_sta(flc->ftl,sys,time,rf_data,no_samples, elem);
	else
	  bf_lines[0] = beamform_line_dynamic(flc->ftl,sys,time,rf_data,no_samples);
				
    }else if(flc->ftl->pixel == TRUE){
      if (alc->atl->no_times > 0)
	bf_lines[0] = beamform_apo_line_pixels(flc->ftl,alc->atl,sys,time,rf_data,no_samples,element_no);
      else
	bf_lines[0] = beamform_line_pixels(flc->ftl,sys,time,rf_data,no_samples,element_no);
    }else{
      if (alc->atl->no_times > 0)
	bf_lines[0] = beamform_apo_line_times(flc->ftl,alc->atl,sys,time,rf_data,no_samples);
      else
	bf_lines[0] = beamform_line_times(flc->ftl,sys,time,rf_data,no_samples);
    }
  }else{
    /* First determine whether we have to call apodize or beamform_apo_ ... */
    for (i = 0; i < alc->no_apo_time_lines; i++)
      if(alc->atl[i].no_times > max_no_apo_times)
	max_no_apo_times = alc->atl[i].no_times;
    if (element_no < 64000 && elem==NULL) {
      elem = flc->ftl[0].xdc->c+element_no;	
    }
    /* Set up for threading*/
#ifndef NOTHREAD
    bf_threads = calloc(flc->no_focus_time_lines,sizeof(pthread_t));
#endif
    bf_thread_info = calloc(flc->no_focus_time_lines,sizeof(BFT_ThreadData));

    bf_thread_info->flc = flc;
    bf_thread_info->alc = alc;
    bf_thread_info->sys = sys;
    bf_thread_info->time = time;
    bf_thread_info->rf_data = rf_data;
    bf_thread_info->no_samples = no_samples;
    bf_thread_info->elem = elem;
    if (max_no_apo_times > 0){
      /* Create threads/run function */
      for(i = 0; i < flc->no_focus_time_lines; i++){
	bf_thread_info[i] = bf_thread_info[0];
	bf_thread_info[i].line = &bf_lines[i];
	bf_thread_info[i].i = i;
#ifndef NOTHREAD
	if (pthread_create(&bf_threads[i],NULL,beamform_thread_apo, (void *)&bf_thread_info[i])) {
	  mexErrMsgTxt("Error creating threads.\n");
	}
#else
	beamform_thread_apo((void *)&bf_thread_info[i]);
#endif
      }
#ifndef NOTHREAD
      /*Join all the threads*/
      for(i = 0; i < flc->no_focus_time_lines; i++){
	pthread_join(bf_threads[i],NULL);
      }
#endif
    }else{

      /* Create threads */
      for(i = 0; i < flc->no_focus_time_lines; i++){
	bf_thread_info[i] = bf_thread_info[0];
	bf_thread_info[i].line = &bf_lines[i];
	bf_thread_info[i].i = i;
#ifndef NOTHREAD
	if (pthread_create(&bf_threads[i],NULL,beamform_thread_noapo, (void *)&bf_thread_info[i])) {
	  mexErrMsgTxt("Error creating threads.\n");
	}
#else
	beamform_thread_noapo((void *)&bf_thread_info[i]);
#endif
      }
#ifndef NOTHREAD
      /*Join all the threads*/
      for(i = 0; i < flc->no_focus_time_lines; i++){
	pthread_join(bf_threads[i],NULL);
      }
#endif
    }
#ifndef NOTHREAD
    free(bf_threads);
#endif
    free(bf_thread_info);
  }
  return bf_lines;
}






/*********************************************************************
 * FUNCTION : add_apo_lines_time
 * ABSTRACT : Add the information from a low-resolution line to a 
 *            a high resolution one
 *********************************************************************/
void add_apo_lines_time(TFocusTimeLine *ftl, TApoTimeLine* atl, TSysParams* sys, 
			double* hi_res, double* lo_res, ui32 element,
			double time,      ui32 no_samples)
{
  double d1, A1;     /* Delay and weighting coefficient */
  double apo1;       /* Apodization value               */
  ui32 id, ind;      /* Index of applied delay          */
  ui32 ia, ina;      /* Index of apodization            */
  ui32 os;           /* Index of output sample          */
  ui32 is1;          /* Indexes of the input samples    */
  ui32 o_abs_s;      /* Absolute index of output sample */
   
   
  /* Filter a little bit the input parameters           */
  if (atl->no_times == 0){
    printf("add_apo_lines_time:\nError: ");
    printf("There must be at least one apodization \n");
    printf("\tNext time call 'bft_apodization'\n");
    abort();
  }
   
  if (ftl->no_times == 0){
    printf("add_apo_lines_time:\n Error: ");
    printf("There must be at least one focus time \n");
    printf("\tNext time call 'bft_focust_times' or 'bft_focus'\n");
    abort();
  }

  if (element > ftl->xdc->no_elements){
    printf("add_apo_lines_time:\n");
    printf("Either the numer of element 1 or element 2 exceeds the number ");
    printf("of elements of the associated transducer\n");
    abort();
  }
   
   
  /* What would the sample index be, if time was =0     */  
  o_abs_s = time*sys->fs;
   
  /* Find the first delay and apodization to apply      */
  id = 0; ind = 1;
  while( ftl->delay[ind].time < o_abs_s) {ind ++; id ++;}
  d1 = ftl->delay[id].d[element]; A1 = ftl->delay[id].a[element];
   
  ia = 0; ina = 1;
  while( atl->a[ina].time < o_abs_s) {ina ++; ia ++;}
  apo1 = atl->a[ia].a[element];
   
  /* Beamform the line - one sample at a time           */
  no_samples --;
   
  for (os = 0; os < no_samples; os ++, o_abs_s++){
    if (o_abs_s > ftl->delay[ind].time){
      ind ++; id ++;
      d1= ftl->delay[id].d[element]; A1= ftl->delay[id].a[element];
    }

    if (o_abs_s > atl->a[ina].time){
      ina ++; ia ++;
      apo1 = atl->a[ia].a[element];
    }
      
    is1 = os - d1; 
      
    if((is1-1) < no_samples && is1 < no_samples)
      /* hi_res[os] += (lo_res[is1]*(1-A1) + lo_res[is1-1]*A1);*/
      hi_res[os] += apo1*(lo_res[is1]*(1-A1) + lo_res[is1-1]*A1);
       
  }
   
}

/*********************************************************************
 * FUNCTION : add_lines_time
 * ABSTRACT : Add the information from a low-resolution line to a 
 *            a high resolution one
 *********************************************************************/
void add_lines_time(TFocusTimeLine *ftl, TSysParams* sys, 
		    double* hi_res, double* lo_res, ui32 element,
		    double time,      ui32 no_samples)
{
  double d1, A1;     /* Delay and weighting coefficient */
  ui32 id, ind;      /* Index of applied delay          */
  ui32 os;           /* Index of output sample          */
  ui32 is1;          /* Indexes of the input samples    */
  ui32 o_abs_s;      /* Absolute index of output sample */
   
   
   
  if (ftl->no_times == 0){
    printf("add_lines_time:\n Error: ");
    printf("There must be at least one focus time \n");
    printf("\tNext time call 'bft_focust_times' or 'bft_focus'\n");
    abort();
  }

  if (element > ftl->xdc->no_elements){
    printf("add_lines_time:\n");
    printf("Either the numer of element 1 or element 2 exceeds the number ");
    printf("of elements of the associated transducer\n");
    abort();
  }
   
  /* What would the sample index be, if time was =0     */  
  o_abs_s = time*sys->fs;
   
  /* Find the first delay and apodization to apply      */
  id = 0; ind = 1;
  while( ftl->delay[ind].time < o_abs_s) {ind ++; id ++;}
  d1 = ftl->delay[id].d[element]; A1 = ftl->delay[id].a[element];
   
   
  /* Beamform the line - one sample at a time           */
  no_samples --;
   
  for (os = 0; os < no_samples; os ++, o_abs_s++){
    if (o_abs_s > ftl->delay[ind].time){
      ind ++; id ++;
      d1= ftl->delay[id].d[element]; A1= ftl->delay[id].a[element];
    }

    is1 = os - d1; 
      
    if((is1-1) < no_samples && is1 < no_samples)
      hi_res[os] += (lo_res[is1]*(1-A1) + lo_res[is1-1]*A1);
  }
   
}


/*********************************************************************
 * FUNCTION : add_images
 * ABSTRACT : Add a low-resoltuion image to one high-resolution image
 *********************************************************************/
void add_images(TFocusLineCollection *flc, TApoLineCollection *alc,
		TSysParams* sys, double **hi_res,
		double **lo_res, ui32 element, 
		double time, ui32 no_samples)


{
  ui32 line_no;
  /*
   *   Filter the input parameters for wrong settings
   */
  if (flc->no_focus_time_lines != alc->no_apo_time_lines){
    printf("\007 add_images:\n");
    printf("Error : the number of apodization lines and the number of ");
    printf("focus lines must be the same \n");
  }
  
  if (flc->no_focus_time_lines == 0){
    printf("\007 add_images:\n");
    printf("Error : the number of defined lines is 0\n");
  }
  
  for ( line_no = 0; line_no < flc->no_focus_time_lines; line_no ++ ){
    if (flc->ftl[line_no].dynamic == TRUE || flc->ftl[line_no].pixel == TRUE){
      printf("add_images:\n");
      printf("Error : unforunately, the addition is not supported \n");
      printf("        for dynamic or pixel based focusing .\n");
      abort();
    }
  }
  
  for (line_no = 0; line_no < flc->no_focus_time_lines; line_no ++){
    if (alc->atl[line_no].no_times > 0){
      add_apo_lines_time(flc->ftl+line_no, alc->atl+line_no, sys, 
			 hi_res[line_no],  lo_res[line_no], element, time, no_samples);
    }else{
      add_lines_time(flc->ftl+line_no,  sys, 
		     hi_res[line_no],  lo_res[line_no], element, time, no_samples);
    }
  }
}






/*********************************************************************
 * FUNCTION : sub_lines_time
 * ABSTRACT : Subtract the information of a low-resolution line from a 
 *            a high resolution one
 *********************************************************************/
void sub_lines_time(TFocusTimeLine *ftl,  TSysParams* sys, 
		    double* hi_res, double* lo_res, ui32 element,
		    double time,      ui32 no_samples)
{
  double d1, A1;     /* Delay and weighting coefficient */
  ui32 id, ind;      /* Index of applied delay          */
  ui32 os;           /* Index of output sample          */
  ui32 is1;          /* Indexes of the input samples    */
  ui32 o_abs_s;      /* Absolute index of output sample */
   
   
  if (ftl->no_times == 0){
    printf("sub_lines_time:\n Error: ");
    printf("There must be at least one focus time \n");
    printf("\tNext time call 'bft_focust_times' or 'bft_focus'\n");
    abort();
  }

  if (element > ftl->xdc->no_elements){
    printf("sub_lines_time:\n");
    printf("Either the numer of element 1 or element 2 exceeds the number ");
    printf("of elements of the associated transducer\n");
    abort();
  }
   
   
  /* What would the sample index be, if time was =0     */  
  o_abs_s = time*sys->fs;
   
  /* Find the first delay and apodization to apply      */
  id = 0; ind = 1;
  while( ftl->delay[ind].time < o_abs_s) {ind ++; id ++;}
  d1 = ftl->delay[id].d[element]; A1 = ftl->delay[id].a[element];
   
   
  /* Beamform the line - one sample at a time           */
  no_samples --;
   
  for (os = 0; os < no_samples; os ++, o_abs_s++){
    if (o_abs_s > ftl->delay[ind].time){
      ind ++; id ++;
      d1= ftl->delay[id].d[element]; A1= ftl->delay[id].a[element];
    }
      
    is1 = os - d1; 
      
    if((is1-1) < no_samples && is1 < no_samples)
      hi_res[os] -= (lo_res[is1]*(1-A1) + lo_res[is1-1]*A1);
       
  }
   
}

/**********************************************************************
 * FUNCTION : sub_apo_lines_time
 * ABSTRACT : Subtract the information of a low-resolution line from a 
 *            a high resolution one
 *********************************************************************/
void sub_apo_lines_time(TFocusTimeLine *ftl, TApoTimeLine* atl, TSysParams* sys, 
			double* hi_res, double* lo_res, ui32 element,
			double time,      ui32 no_samples)
{
  double d1, A1;     /* Delay and weighting coefficient */
  double apo1;       /* Apodization value               */
  ui32 id, ind;      /* Index of applied delay          */
  ui32 ia, ina;      /* Index of apodization            */
  ui32 os;           /* Index of output sample          */
  ui32 is1;          /* Indexes of the input samples    */
  ui32 o_abs_s;      /* Absolute index of output sample */
   
   
  /* Filter a little bit the input parameters           */
  if (atl->no_times == 0){
    printf("sub_lines_time:\nError: ");
    printf("There must be at least one apodization \n");
    printf("\tNext time call 'bft_apodization'\n");
    abort();
  }
   
  if (ftl->no_times == 0){
    printf("sub_lines_time:\n Error: ");
    printf("There must be at least one focus time \n");
    printf("\tNext time call 'bft_focust_times' or 'bft_focus'\n");
    abort();
  }

  if (element > ftl->xdc->no_elements){
    printf("sub_lines_time:\n");
    printf("Either the numer of element 1 or element 2 exceeds the number ");
    printf("of elements of the associated transducer\n");
    abort();
  }
   
   
  /* What would the sample index be, if time was =0     */  
  o_abs_s = time*sys->fs;
   
  /* Find the first delay and apodization to apply      */
  id = 0; ind = 1;
  while( ftl->delay[ind].time < o_abs_s) {ind ++; id ++;}
  d1 = ftl->delay[id].d[element]; A1 = ftl->delay[id].a[element];
   
  ia = 0; ina = 1;
  while( atl->a[ina].time < o_abs_s) {ina ++; ia ++;}
  apo1 = atl->a[ia].a[element];
   
  /* Beamform the line - one sample at a time           */
  no_samples --;
   
  for (os = 0; os < no_samples; os ++, o_abs_s++){
    if (o_abs_s > ftl->delay[ind].time){
      ind ++; id ++;
      d1= ftl->delay[id].d[element]; A1= ftl->delay[id].a[element];
    }

    if (o_abs_s > atl->a[ina].time){
      ina ++; ia ++;
      apo1 = atl->a[ia].a[element];
    }
      
    is1 = os - d1; 
      
    if((is1-1) < no_samples && is1 < no_samples)
      hi_res[os] -= apo1*(lo_res[is1]*(1-A1) + lo_res[is1-1]*A1);
       
  }
   
}


/*********************************************************************
 * FUNCTION : sub_images
 * ABSTRACT : Subtract a low-resoltuion image from one high-resolution image
 *********************************************************************/
void sub_images(TFocusLineCollection *flc, TApoLineCollection *alc,
		TSysParams* sys, double **hi_res,
		double **lo_res, ui32 element, 
		double time, ui32 no_samples)


{
  ui32 line_no;
  /*
   *   Filter the input parameters for wrong settings
   */
  if (flc->no_focus_time_lines != alc->no_apo_time_lines){
    printf("\007 sub_images:\n");
    printf("Error : the number of apodization lines and the number of ");
    printf("focus lines must be the same \n");
  }
  
  if (flc->no_focus_time_lines == 0){
    printf("\007 sub_image:\n");
    printf("Error : the number of defined lines is 0\n");
  }
  
   
  for (line_no = 0; line_no < flc->no_focus_time_lines; line_no ++){
     
    if (alc->atl[line_no].no_times > 0){
      sub_apo_lines_time(flc->ftl+line_no, alc->atl+line_no, sys, 
			 hi_res[line_no],  lo_res[line_no], element, time, no_samples);
    }else{
      sub_lines_time(flc->ftl+line_no,  sys, 
		     hi_res[line_no],  lo_res[line_no], element, time, no_samples);
    }               
  }
}

