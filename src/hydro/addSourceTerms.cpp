
#include "../idefix.hpp"
#include "hydro.hpp"

// Add source terms
void Hydro::AddSourceTerms(DataBlock &data, real t, real dt) {

    idfx::pushRegion("Hydro::AddSourceTerms");
    IdefixArray4D<real> Uc = data.Uc;
    IdefixArray4D<real> Vc = data.Vc;
    IdefixArray1D<real> x1 = data.x[IDIR]; 
    IdefixArray1D<real> x2 = data.x[JDIR]; 
    #if GEOMETRY == SPHERICAL
    IdefixArray1D<real> s  = data.s;
    IdefixArray1D<real> rt = data.rt;
    #endif

    real OmegaX1 = this->OmegaX1;
    real OmegaX2 = this->OmegaX2;
    real OmegaX3 = this->OmegaX3;
    bool haveRotation = this->haveRotation;
    
    idefix_for("AddSourceTerms",data.beg[KDIR],data.end[KDIR],data.beg[JDIR],data.end[JDIR],data.beg[IDIR],data.end[IDIR],
        KOKKOS_LAMBDA (int k, int j, int i) {

            #if GEOMETRY == CARTESIAN
                if(haveRotation) {
                    #if COMPONENTS == 3
                    Uc(MX1,k,j,i) += TWO_F * dt * Vc(RHO,k,j,i) * (OmegaX3 * Vc(VX2,k,j,i) - OmegaX2 * Vc(VX3,k,j,i));
                    Uc(MX2,k,j,i) += TWO_F * dt * Vc(RHO,k,j,i) * (OmegaX1 * Vc(VX3,k,j,i) - OmegaX3 * Vc(VX1,k,j,i));
                    Uc(MX3,k,j,i) += TWO_F * dt * Vc(RHO,k,j,i) * (OmegaX2 * Vc(VX1,k,j,i) - OmegaX1 * Vc(VX2,k,j,i));
                    #endif
                    #if COMPONENTS == 2
                    Uc(MX1,k,j,i) += TWO_F * dt * Vc(RHO,k,j,i) * (   OmegaX3 * Vc(VX2,k,j,i) );
                    Uc(MX2,k,j,i) += TWO_F * dt * Vc(RHO,k,j,i) * ( - OmegaX3 * Vc(VX1,k,j,i) );
                    #endif

                }
            #elif GEOMETRY == CYLINDRICAL
                #if COMPONENTS == 3
                    real vphi,Sm;
                    vphi = Vc(iVPHI,k,j,i);
                    if(haveRotation) vphi += OmegaX3*x1(i);
                    Sm = Vc(RHO,k,j,i) * vphi*vphi; // Centrifugal     
                    Sm += Vc(PRS,k,j,i);            // Presure (because pressure is included in the flux, additional source terms arise)
                    #if MHD==YES
                        Sm -=  Vc(iBPHI,k,j,i)*Vc(iBPHI,k,j,i); // Hoop stress
                        Sm += HALF_F*(EXPAND(Vc(BX1,k,j,i)*Vc(BX1,k,j,i) , +Vc(BX2,k,j,i)*Vc(BX2,k,j,i), +Vc(BX3,k,j,i)*Vc(BX3,k,j,i))); // Magnetic pressure
                    #endif // MHD
                    Uc(MX1,k,j,i) += dt * Sm / x1(i);
                #endif // COMPONENTS
            #elif GEOMETRY == POLAR
                real vphi,Sm;
                vphi = Vc(iVPHI,k,j,i);
                if(haveRotation) vphi += OmegaX3*x1(i);
                Sm = Vc(RHO,k,j,i) * vphi*vphi;     // Centrifugal
                Sm += Vc(PRS,k,j,i);               // Pressure (because we're including pressure in the flux, we need that to get the radial pressure gradient)       
                #if MHD==YES
                    Sm -=  Vc(iBPHI,k,j,i)*Vc(iBPHI,k,j,i); // Hoop stress
                    // Magnetic pressus
                    Sm += HALF_F*(EXPAND(Vc(BX1,k,j,i)*Vc(BX1,k,j,i) , +Vc(BX2,k,j,i)*Vc(BX2,k,j,i), +Vc(BX3,k,j,i)*Vc(BX3,k,j,i)));
                #endif // MHD
                Uc(MX1,k,j,i) += dt * Sm / x1(i);

            #elif GEOMETRY == SPHERICAL
                real vphi,Sm,ct; 
                vphi = SELECT(ZERO_F, ZERO_F, Vc(iVPHI,k,j,i));
                if(haveRotation) vphi += OmegaX3*x1(i)*s(j);
                Sm = Vc(RHO,k,j,i) * (EXPAND( ZERO_F, + Vc(VX2,k,j,i)*Vc(VX2,k,j,i), + vphi*vphi)); // Centrifugal
                Sm += 2.0*Vc(PRS,k,j,i);    // Pressure curvature
                #if MHD == YES
                    Sm -= EXPAND( ZERO_F, + Vc(iBTH,k,j,i)*Vc(iBTH,k,j,i), + Vc(iBPHI,k,j,i)*Vc(iBPHI,k,j,i)); // Hoop stress
                    Sm += EXPAND(Vc(BX1,k,j,i)*Vc(BX1,k,j,i) , +Vc(BX2,k,j,i)*Vc(BX2,k,j,i), +Vc(BX3,k,j,i)*Vc(BX3,k,j,i)); // 2* mag pressure curvature
                #endif
                Uc(MX1,k,j,i) += dt*Sm/x1(i);
                
                ct = 1.0/TAN(x2(j));
                Sm = Vc(RHO,k,j,i) * (EXPAND( ZERO_F, - Vc(iVTH,k,j,i)*Vc(iVR,k,j,i), + ct*vphi*vphi)); // Centrifugal
                Sm += ct * Vc(PRS,k,j,i);       // Pressure curvature
                #if MHD == YES
                    Sm += EXPAND( ZERO_F, + Vc(iBTH,k,j,i)*Vc(iBR,k,j,i), - ct*Vc(iBPHI,k,j,i)*Vc(iBPHI,k,j,i)); // Hoop stress
                    Sm += HALF_F*ct*EXPAND(Vc(BX1,k,j,i)*Vc(BX1,k,j,i) , +Vc(BX2,k,j,i)*Vc(BX2,k,j,i), +Vc(BX3,k,j,i)*Vc(BX3,k,j,i)); // Magnetic pressure
                #endif
                Uc(MX2,k,j,i) += dt*Sm / rt(i);
            #endif

        });
    
    idfx::popRegion();
    
}