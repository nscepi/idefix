#include "../idefix.hpp"
#include "hydro.hpp"

// Evolve the magnetic field in Vs according to Constranied transport
void Hydro::EvolveMagField(DataBlock &data, real t, real dt) {
    idfx::pushRegion("Hydro::EvolveMagField");

    // Corned EMFs
    IdefixArray3D<real> Ex1 = data.emf.ex;
    IdefixArray3D<real> Ex2 = data.emf.ey;
    IdefixArray3D<real> Ex3 = data.emf.ez;

    // Field
    IdefixArray4D<real> Vs = data.Vs;

    // Coordinates
    IdefixArray1D<real> x1=data.x[IDIR];
    IdefixArray1D<real> x2=data.x[JDIR];
    IdefixArray1D<real> x3=data.x[KDIR];

    IdefixArray1D<real> x1p=data.xr[IDIR];
    IdefixArray1D<real> x2p=data.xr[JDIR];
    IdefixArray1D<real> x3p=data.xr[KDIR];

    IdefixArray1D<real> x1m=data.xl[IDIR];
    IdefixArray1D<real> x2m=data.xl[JDIR];
    IdefixArray1D<real> x3m=data.xl[KDIR];

    IdefixArray1D<real> dx1=data.dx[IDIR];
    IdefixArray1D<real> dx2=data.dx[JDIR];
    IdefixArray1D<real> dx3=data.dx[KDIR];



    idefix_for("EvolvMagField",data.beg[KDIR],data.end[KDIR]+KOFFSET,data.beg[JDIR],data.end[JDIR]+JOFFSET,data.beg[IDIR],data.end[IDIR]+IOFFSET,
                    KOKKOS_LAMBDA (int k, int j, int i) {
                        
                        real rhsx1, rhsx2, rhsx3;

                        #if GEOMETRY == CARTESIAN
                            rhsx1 = D_EXPAND( ZERO_F                                     ,
                                            - dt/dx2(j) * (Ex3(k,j+1,i) - Ex3(k,j,i) )  ,
                                            + dt/dx3(k) * (Ex2(k+1,j,i) - Ex2(k,j,i) ) );
                                            
                            #if DIMENSIONS >= 2
                            rhsx2 =  D_EXPAND(   dt/dx1(i) * (Ex3(k,j,i+1) - Ex3(k,j,i) )  ,
                                                                                            ,
                                                - dt/dx3(k) * (Ex1(k+1,j,i) - Ex1(k,j,i) ) );
                            #endif
                            #if DIMENSIONS == 3
                            rhsx3 =    - dt/dx1(i) * (Ex2(k,j,i+1) - Ex2(k,j,i) )  
                                       + dt/dx2(j) * (Ex1(k,j+1,i) - Ex1(k,j,i) ) );
                            #endif

                        #elif GEOMETRY == CYLINDRICAL
                            rhsx1 = - dt/dx2(j) * (Ex3(k,j+1,i) - Ex3(k,j,i) );
                            #if DIMENSIONS >= 2
                            rhsx2 = dt * (FABS(x1p(i)) * Ex3(k,j,i+1) - FABS(x1m(i)) * Ex3(k,j,i)) / FABS(x1(i)*dx1(i));
                            #endif

                        #elif GEOMETRY == POLAR

                            rhsx1 = D_EXPAND( ZERO_F                                     ,
                                            - dt/(FABS(x1m(i)) * dx2(j)) * (Ex3(k,j+1,i) - Ex3(k,j,i) )  ,
                                            + dt/dx3(k) * (Ex2(k+1,j,i) - Ex2(k,j,i) ) );

                            #if DIMENSIONS >= 2
                            rhsx2 =  D_EXPAND(   dt/dx1(i) * (Ex3(k,j,i+1) - Ex3(k,j,i) )  ,
                                                                                            ,
                                                - dt/dx3(k) * (Ex1(k+1,j,i) - Ex1(k,j,i) ) );
                            #endif
                            #if DIMENSIONS == 3
                            rhsx3 =    dt/(FABS(x1(i))) * (
                                            -  (x1m(i+1)*Ex2(k,j,i+1) - x1m(i)*Ex2(k,j,i) ) / dx1(i) 
                                            +  (Ex1(k,j+1,i) - Ex1(k,j,i) ) / dx2(j) );
                            #endif

                        #elif GEOMETRY == SPHERICAL
                            real dV2 = FABS(cos(x2m(j)) - cos(x2p(j)));
                            real Ax2p = FABS(sin(x2p(j)));
                            real Ax2m = FABS(sin(x2m(j)));

                            rhsx1 = D_EXPAND( ZERO_F                                     ,
                                            - dt/(x1p(i)*dV2) * ( Ax2p*Ex3(k,j+1,i) - Ax2m*Ex3(k,j,i) )  ,
                                            + dt*dx2(j)/(x1m(i)*dV2*dx3(k))*(Ex2(k+1,j,i) - Ex2(k,j,i) ) );
                                            
                            #if DIMENSIONS >= 2
                            rhsx2 =  D_EXPAND(   dt/(x1(i)*dx1(i)) * (x1m(i+1)*Ex3(k,j,i+1) - x1m(i)*Ex3(k,j,i) )  ,
                                                                                            ,
                                                - dt/(x1(i)*Ax2m*dx3(k)) * (Ex1(k+1,j,i) - Ex1(k,j,i) ) );
                            #endif
                            #if DIMENSIONS == 3
                            rhsx3 =    - dt/(x1(i)*dx1(i)) * (x1m(i+1)*Ex2(k,j,i+1) - x1m(i)*Ex2(k,j,i) )  
                                       + dt/(x1(i)*dx2(j)) * (Ex1(k,j+1,i) - Ex1(k,j,i) ) );                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     * (Ex2(k+1,j,i) - Ex2(k,j,i) ) );
                            #endif
                        #endif



                        Vs(BX1s,k,j,i) = Vs(BX1s,k,j,i) + rhsx1;

                        #if DIMENSIONS >= 2
                        Vs(BX2s,k,j,i) = Vs(BX2s,k,j,i) + rhsx2;
                        #endif
                        #if DIMENSIONS == 3
                        Vs(BX3s,k,j,i) = Vs(BX3s,k,j,i) + rhsx3;
                        #endif

                    });
    idfx::popRegion();
}