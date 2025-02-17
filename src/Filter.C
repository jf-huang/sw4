// -*-c++-*-
//  SW4 LICENSE
// # ----------------------------------------------------------------------
// # SW4 - Seismic Waves, 4th order
// # ----------------------------------------------------------------------
// # Copyright (c) 2013, Lawrence Livermore National Security, LLC. 
// # Produced at the Lawrence Livermore National Laboratory. 
// # 
// # Written by:
// # N. Anders Petersson (petersson1@llnl.gov)
// # Bjorn Sjogreen      (sjogreen2@llnl.gov)
// # 
// # LLNL-CODE-643337 
// # 
// # All rights reserved. 
// # 
// # This file is part of SW4, Version: 1.0
// # 
// # Please also read LICENCE.txt, which contains "Our Notice and GNU General Public License"
// # 
// # This program is free software; you can redistribute it and/or modify
// # it under the terms of the GNU General Public License (as published by
// # the Free Software Foundation) version 2, dated June 1991. 
// # 
// # This program is distributed in the hope that it will be useful, but
// # WITHOUT ANY WARRANTY; without even the IMPLIED WARRANTY OF
// # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the terms and
// # conditions of the GNU General Public License for more details. 
// # 
// # You should have received a copy of the GNU General Public License
// # along with this program; if not, write to the Free Software
// # Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA 
#include <mpi.h>

#include <iostream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <complex>

#include "Filter.h"
#include "Require.h"

using namespace std;

Filter::Filter(FilterType type, unsigned int numberOfPoles, unsigned int numberOfPasses, float_sw4 f1, float_sw4 f2)
{
  m_type = type;
  m_poles = numberOfPoles;
  CHECK_INPUT(m_poles > 0,"Filter: Number of poles must be positive, not " << m_poles);
  m_passes = numberOfPasses;
  CHECK_INPUT(m_passes >= 1 && m_passes <= 2,"Filter: Number of passes must be 1 or 2, not " << m_passes);
  m_f1 = f1;
  m_f2 = f2;
  m_numberOfSOS = 0;
  m_real_poles = 0;
  m_complex_pairs = 0;
// need to call setDt when the time step becomes available
  m_dt = 0.;
  m_initialized = false;
  m_pole_min_re = 1e10;
} // end constructor

// destructor
Filter::~Filter()
{
  for (int q=0; q<m_numberOfSOS; q++)
    delete m_SOSp[q];
}

void Filter::computeSOS(float_sw4 dt)
{
  m_dt = dt;
  CHECK_INPUT( m_dt > 0., "Filter::computeSOS: non-positive time step!");
  
// separation in angle between poles in prototype filter
  float_sw4 dAlpha = M_PI/m_poles;
  
// odd or even order?
  if (2*(m_poles/2) == m_poles)
  {
    m_complex_pairs = m_poles/2;
  }
  else
  {
    m_real_poles = 1;
    m_complex_pairs = (m_poles-1)/2;
  }
  
// build the second order sections corresponding to the poles
  SecondOrderSection *sos1_ptr, *sos2_ptr;
  float_sw4 alpha0, pole_re=1e10;
  
  if (m_real_poles == 1)
  {
    if (m_type == bandPass)
    {
      m_pole_min_re = realPoleBP(m_f1, m_f2, m_dt, sos1_ptr);
    }
    else if (m_type == lowPass)
    {
      m_pole_min_re = realPoleLP(m_f2, m_dt, sos1_ptr);
    }    
    m_SOSp.push_back(sos1_ptr);
    
    alpha0= M_PI - dAlpha;	
  }
  else
  {
    alpha0=M_PI - 0.5*dAlpha;
  }
  for (int q=0; q<m_complex_pairs; q++)
  {
    if (m_type == bandPass)
    {
      pole_re = complexConjugatedPolesBP(m_f1, m_f2, m_dt, alpha0, sos1_ptr, sos2_ptr);
      m_SOSp.push_back(sos1_ptr);
      m_SOSp.push_back(sos2_ptr);
    }
    else if (m_type == lowPass)
    {
      pole_re = complexConjugatedPolesLP(m_f2, m_dt, alpha0, sos1_ptr);
      m_SOSp.push_back(sos1_ptr);
    }  
    m_pole_min_re = min(pole_re, m_pole_min_re);
      
    alpha0 -= dAlpha;
  }
  m_initialized = true;

   
// this variable is for convenience
  m_numberOfSOS = m_SOSp.size();
  
}

// output all coefficients
ostream& operator<<( ostream& output, const Filter& s )
{
  if (s.m_type == lowPass)
    output << "Lowpass";
  else
    output << "Bandpass";
  
  output << " filter of order " << s.m_poles << 
    " corner freq 1 = " << s.m_f1 << " corner freq 2 = " << s.m_f2 << " passes = " << s.m_passes << endl <<
    "The filter consists of " << s.m_SOSp.size() << " second order sections:" << endl;
  for (int q=0; q<s.m_SOSp.size(); q++)
  {
    SecondOrderSection *sos_ptr = s.m_SOSp[q];
//    printf("sos_ptr = %d\n", sos_ptr);
    output << "Numerator coefficients: " << sos_ptr->m_n << endl;
    output << "Denominator coefficients: " << sos_ptr->m_d << endl;
  }
  output << "Estimated decay rate of filter exp(-alpha*t), alpha = " << s.m_pole_min_re << endl;

  return output;
}


float_sw4 Filter::realPoleBP(float_sw4 f1, float_sw4 f2, float_sw4 dt, SecondOrderSection *&sos_ptr)
{
// pre-warp the corner frequencies
  float_sw4 om1 = tan(M_PI*dt*f1);
  float_sw4 om2 = tan(M_PI*dt*f2);

//  printf("RP_BP: Input corner frequencies f1=%e, f2=%e, pre-warped om1=%e, om2=%e, time step=%e\n", 
//	 f1, f2, om1, om2, dt);

  float_sw4 b = om2 - om1;
  float_sw4 p = om1*om2;

//  pole in proptotype filter: s = -1, with transfer function H(s)=1/(s+1)

// Analog filter is obtained as H(Tbp(s)), where Tbp(s) = (p + s^2)/(b*s)
//  analog bp filter coeff transfer fcn are saved as N(s)/D(s), 
//  N(s) = n[0] + n[1]*s + n[2]*s^2
//  D(s) = d[0] + d[1]*s + d[2]*s^2

//  storage for analog filter
  float_sw4 n1[3], d1[3];

//  These are the coefficients in SOS #1 (Numerator and denominator)
  n1[0] = 0;
  n1[1] = b;
  n1[2] = 0;
  d1[0] = p;
  d1[1] = b;
  d1[2] = 1;

//  allocate space for output arrays
  Polynomial a1, b1;

//  transform analog to digital by the transformation AD(s) = (1-s)/(1+s)
  a2d(n1, d1, b1, a1);
  sos_ptr = new SecondOrderSection( b1, a1 );

// estimate decay rate
  float_sw4 dscr = b*b - 4*p, pole_min_re = 0;
  if (dscr <= 0.)
    pole_min_re = fabs(0.5*b)*2/dt;
  else
    pole_min_re = fabs(0.5*(-b + sqrt(dscr)))*2/dt;
  
//  printf("Real pole: dscr=%e, b=%e, p=%e, decay rate estimate exp(-alpha*t), alpha = %e\n", dscr, b, p, pole_min_re);

  return pole_min_re;

}

float_sw4 Filter::realPoleLP(float_sw4 fc, float_sw4 dt, SecondOrderSection *&sos_ptr)
{
// pre-warp the corner frequency
  float_sw4 omc = tan(M_PI*dt*fc);

//  printf("RP_LP: Input corner frequency fc=%e, pre-warped omc=%e, time step=%e\n", 
//	 fc, omc, dt);

//  pole in proptotype filter: s = -1, with transfer function H(s)=1/(s+1)

// Analog filter is obtained as H(Tbp(s)), where Tbp(s) = s/omc
//  analog bp filter coeff transfer fcn are saved as N(s)/D(s), 
//  N(s) = n[0] + n[1]*s + n[2]*s^2
//  D(s) = d[0] + d[1]*s + d[2]*s^2

//  storage for analog filter
  float_sw4 n1[3], d1[3];

//  These are the coefficients in the SOS (Numerator and denominator)
  n1[0] = omc;
  n1[1] = 0;
  n1[2] = 0;
  d1[0] = omc;
  d1[1] = 1;
  d1[2] = 0;

//  allocate space for output arrays
  Polynomial a1, b1;

//  transform analog to digital by the transformation AD(s) = (1-s)/(1+s)
  a2d(n1, d1, b1, a1);
  sos_ptr = new SecondOrderSection( b1, a1 );

// estimate decay rate
  float_sw4 pole_min_re = 0;
  pole_min_re = fabs(omc)*2/dt;
  
//  printf("Real pole LP: decay rate estimate exp(-alpha*t), alpha = %e\n", pole_min_re);

  return pole_min_re;
}


float_sw4 Filter::complexConjugatedPolesBP(float_sw4 f1, float_sw4 f2, float_sw4 dt, float_sw4 alpha, 
			      SecondOrderSection *&sos1_ptr, SecondOrderSection *&sos2_ptr)
{
// Input: 
//        f1: low corner frequency [Hz]
//        f2: high corner frequency [Hz]
//        dt: time step [s] of the time series (to be filtered),
//        alpha: angle of the pole [rad] (pi/2 < alpha < pi).
// 
// Output: 
//         b1(1:3): numerator coefficients, SOS 1
//         a1(1:3): denominator coefficients with a1(1)=1, SOS 1
//         b2(1:3): numerator coefficients, SOS 2
//         a2(1:3): denominator coefficients with a2(1)=1, SOS 2
//
// return min_pole_re: decay rate estimate
  CHECK_INPUT(alpha < M_PI && alpha > M_PI/2, "pole angle alpha = " << alpha << " out of range");

  float_sw4 pole_min_re=0.;

// imaginary unit
  complex<float_sw4> iu(0.,1.);

//pre-warp the corner frequencies
  float_sw4 om1 = tan(M_PI*dt*f1);
  float_sw4 om2 = tan(M_PI*dt*f2);

//  printf("CCP_BP: Input corner frequencies f1=%e, f2=%e, pre-warped om1=%e, om2=%e, time step=%e\n", 
//	 f1, f2, om1, om2,dt);
  
  float_sw4 b = om2 - om1;
  float_sw4 p = om1*om2;

// pole #1
  complex<float_sw4> q(cos(alpha),sin(alpha));

// analog bp filter coeff transfer fcn are saved as N(s)/D(s), 
// N(s) = n(1) + n(2)*s + n(3)*s^2
// D(s) = d(1) + d(2)*s + d(3)*s^2

// initialize storage
  float_sw4 n1[3], n2[3], d1[3], d2[3];
  for (int q=0; q<3; q++)
  {
    n1[q] = 0;
    n2[q] = 0;
    d1[q] = 0;
    d2[q] = 0;
  }

// roots of the two quadratics
  complex<float_sw4> s1 = 0.5*(q*b + sqrt(pow(q,2)*b*b - 4*p));
  complex<float_sw4> s2 = 0.5*(q*b - sqrt(pow(q,2)*b*b - 4*p));
// these are for testing only
//  complex<float_sw4> s3 = 0.5*(conj(q)*b + sqrt(pow(conj(q),2)*b*b - 4*p));
//  complex<float_sw4> s4 = 0.5*(conj(q)*b - sqrt(pow(conj(q),2)*b*b - 4*p));

// if Re(s1) >= 0 or Re(s2)>= the filter is unstable
  if (real(s1) >= 0 || real(s2) >= 0)
  {
    printf("WARNING: the analog filter has poles in the positive half-plane. s1=%e%+ei, s2=%e%+ei\n", real(s1), imag(s1), real(s2), imag(s2));
  }
  else
  {
    pole_min_re = min(fabs(real(s1)*2/dt), fabs(real(s2)*2/dt));
//    printf("Complex conjugated pole: decay rate estimate exp(-alpha*t), alpha = %e\n", pole_min_re);
  }
  
 
// check the algebra
//printf("P1: q*b=%e%+ei, s1+s2=%e%+ei, s1*s2=%e%+ei, p=%e\n", real(q*b), imag(q*b), real(s1+s2), imag(s1+s2), real(s1*s2), imag(s1*s2), p);
//printf("P2: conj(q)*b=%e%+ei, s3+s4=%e%+ei, s3*s4=%e%+ei, p=%e\n", real(conj(q)*b), imag(conj(q)*b), real(s3+s4), imag(s3+s4), real(s3*s4), imag(s3*s4), p);
//printf("Q1: 2*Re(s1) = %e, |s1|^2 = %e\n", 2*real(s1), abs(s1)^2)
//printf("Q2: 2*Re(s2) = %e, |s2|^2 = %e\n", 2*real(s2), abs(s2)^2)
//printf("Q1Q2, s^3: %e, s^2: %e, s^1: %e, s^0: %e\n", -2*(real(s1)+real(s2)), abs(s1)^2+abs(s2)^2+4*real(s1)*real(s2), -2*(real(s1)*abs(s2)^2 + real(s2)*abs(s1)^2), abs(s1)^2*abs(s2)^2);
//printf("P1P2, s^3: %e, s^2: %e, s^1: %e, s^0: %e\n", -b*(q+conj(q)), 2*p + b^2*q*conj(q), -b*p*(q+conj(q)), p^2);

// These are the coefficients in SOS #1 (Numerator and denominator)
  n1[1] = b;
  d1[0] = pow(abs(s1),2);
  d1[1] = -2*real(s1);
  d1[2] = 1;

// These are the coefficients in SOS #2 (Numerator and denominator)
  n2[1] = b;
  d2[0] = pow(abs(s2),2);
  d2[1] = -2*real(s2);
  d2[2] = 1;

// allocate space for output arrays
  Polynomial a1, a2, b1, b2;

// analog to digial transformation for the first SOS
  a2d(n1, d1, b1, a1);
  sos1_ptr = new SecondOrderSection(b1, a1);

// analog to digial transformation for the second SOS
  a2d(n2, d2, b2, a2);
  sos2_ptr = new SecondOrderSection(b2, a2);

  return pole_min_re;
}

float_sw4 Filter::complexConjugatedPolesLP(float_sw4 fc, float_sw4 dt, float_sw4 alpha, 
					SecondOrderSection *&sos_ptr)
{
// Input: 
//        fc: corner frequency [Hz]
//        dt: time step [s] of the time series (to be filtered),
//        alpha: angle of the pole [rad] (pi/2 < alpha < pi).
// 
// Output: 
//        sos_ptr: pointer to a new Second order section
//
// return min_pole_re: decay rate estimate
  CHECK_INPUT(alpha < M_PI && alpha > M_PI/2, "pole angle alpha = " << alpha << " out of range");

  float_sw4 pole_min_re=0.;

//pre-warp the corner frequencies
  float_sw4 omc = tan(M_PI*dt*fc);

//  printf("CCP_LP: Input corner frequency fc=%e, pre-warped omc=%e, time step=%e\n", 
//	 fc, omc, dt);
  
// pole in prototype filter
  complex<float_sw4> q(cos(alpha),sin(alpha));

// analog lp filter coeff transfer fcn are saved as N(s)/D(s), 
// N(s) = n[0] + n[1]*s + n[2]*s^2
// D(s) = d[0] + d[1]*s + d[2]*s^2

// initialize storage
  float_sw4 n1[3], d1[3];
  for (int q=0; q<3; q++)
  {
    n1[q] = 0;
    d1[q] = 0;
  }

  pole_min_re = fabs(omc*real(q)*2/dt);
//  printf("Complex conjugated pole: decay rate estimate exp(-alpha*t), alpha = %e\n", pole_min_re);

// These are the coefficients in the SOS (Numerator and denominator)
  n1[0] = omc*omc;
  d1[0] = omc*omc;
  d1[1] = -2*omc*real(q);
  d1[2] = 1;

// allocate space for output arrays
  Polynomial a1, b1;

// analog to digial transformation for the SOS
  a2d(n1, d1, b1, a1);
  sos_ptr = new SecondOrderSection(b1, a1);

  return pole_min_re;
}

void Filter::a2d(float_sw4 n[3], float_sw4 d[3], Polynomial &b, Polynomial &a)
{
// transform analog to digital by the transformation AD(s) = (1-s)/(1+s)
// analog filter has transfer fcn H(s) = (n[0] + n[1]*s + n[2]*s^2)/(d[0] + d[1]*s + d[2]*s^2)
// digital filter has normalized transfer function H(AD(s)) = (b[0] + b[1]*s + b[2]*s^2)/(a[0] + a[1]*s + a[2]*s^2), a[0] = 1
// normalization factor
  float_sw4 c = d[0] + d[1] + d[2]; 
// denominator
  a.m_c[0] = 1;
  a.m_c[1] = 2*(d[0]-d[2])/c;
  a.m_c[2] = (d[0] - d[1] + d[2])/c;
// nominator
  b.m_c[0] = (n[0] + n[1] + n[2])/c;
  b.m_c[1] = 2*(n[0]-n[2])/c;
  b.m_c[2] = (n[0] - n[1] + n[2])/c;
} // end a2d

//
float_sw4 Filter::estimatePrecursor()
{
  CHECK_INPUT(m_initialized, "Filter::estimatePrecursor called before filter was initialized");
  
  float_sw4 timeScale=0;

// there is only a precursor if the filter is applied twice (forwards + backwards)
  if (m_passes == 2)
  {
    if (m_pole_min_re > 0.)
      timeScale = 12./m_pole_min_re;
  }
  
  return timeScale;
}


//
void Filter::evaluate(int N, float_sw4 *u, float_sw4 *mf)
// Input: N: size of arrays u and mf
//        u[i]: signal to be filtered
// Output: mf[i]: filtered signal
//
// Note: u and mf can be the same array, in which case the filtered signal overwrites the original signal
{
  int q, i;
  float_sw4 a[3], b[3], wn, wn1, wn2, op;
  float_sw4 x1, x2, y1, y2;
  
  CHECK_INPUT( m_initialized, "Filter::zerophase: filter is NOT initialized!");

  SecondOrderSection *sos_ptr;
  
  if (mf != u)
  {
#pragma omp parallel for
    for (int i=0; i<N; i++)
      mf[i] = u[i];
  }
  
// first do the forwards filtering
// loop over all second order sections
  for (q=0; q<m_SOSp.size(); q++)
  {
    sos_ptr=m_SOSp[q];
    for (i=0; i<3; i++)
    {
      b[i] = sos_ptr->m_n.m_c[i];
      a[i] = sos_ptr->m_d.m_c[i];
    }
// direct form II
    // wn1 = 0;
    // wn2 = 0;
    // for (i=0; i<N; i++)
    // {
    //   wn = mf[i] - a[1]*wn1 - a[2]*wn2;
    //   mf[i] = b[0]*wn + b[1]*wn1 + b[2]*wn2;
    //   wn2 = wn1;
    //   wn1 = wn;
    // }
// forwards, direct form I
    x1=mf[0];
    x2=mf[0];
    y1=mf[0];
    y2=mf[0];
    for (i=0; i<N; i++)
    {
      op = b[0]*mf[i] + b[1]*x1 + b[2]*x2 - (a[1]*y1 + a[2]*y2);
      y2=y1;
      y1=op;
      x2=x1;
      x1=mf[i];
      mf[i]=op;
    }
  }

  if (m_passes == 2)
  {
// then do the backwards filtering
// loop over all second order sections
  
    for (q=0; q<m_SOSp.size(); q++)
    {
      sos_ptr=m_SOSp[q];
      for (i=0; i<3; i++)
      {
	b[i] = sos_ptr->m_n.m_c[i];
	a[i] = sos_ptr->m_d.m_c[i];
      }
// Don't know how to set initial conditions for the direct form II filter
// to avoid transients when the final stage is non-zero
// Using the direct form I algorithm instead
      x1=mf[N-1];
      x2=mf[N-1];
      y1=mf[N-1];
      y2=mf[N-1];
      for (i=N-1; i>=0; i--)
      {
	op = b[0]*mf[i] + b[1]*x1 + b[2]*x2 - (a[1]*y1 + a[2]*y2);
	y2=y1;
	y1=op;
	x2=x1;
	x1=mf[i];
	mf[i]=op;
      }

    }
  } // end 2nd pass (backwards
  
} // end zerophase
