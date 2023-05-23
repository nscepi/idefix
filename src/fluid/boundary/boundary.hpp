// ***********************************************************************************
// Idefix MHD astrophysical code
// Copyright(C) Geoffroy R. J. Lesur <geoffroy.lesur@univ-grenoble-alpes.fr>
// and other code contributors
// Licensed under CeCILL 2.1 License, see COPYING for more information
// ***********************************************************************************

#ifndef FLUID_BOUNDARY_BOUNDARY_HPP_
#define FLUID_BOUNDARY_BOUNDARY_HPP_
#include <string>
#include <vector>
#include "idefix.hpp"
#include "fluid_defs.hpp"
#include "grid.hpp"

#ifdef WITH_MPI
#include "mpi.hpp"
#endif

// forward class declaration
class DataBlock;
#include "physics.hpp"
template <typename Phys> class Fluid;

template<typename Phys>
class Boundary {
 public:
  Boundary(Input &, Grid &, Fluid<Phys>* );
  void SetBoundaries(real);                         ///< Set the ghost zones in all directions
  void EnforceBoundaryDir(real, int);             ///< write in the ghost zone in specific direction
  void ReconstructVcField(IdefixArray4D<real> &);  ///< reconstruct cell-centered magnetic field
  void ReconstructNormalField(int dir);           ///< reconstruct normal field using divB=0

  void EnforceFluxBoundaries(int dir);        ///< Apply boundary condition conditions to the fluxes

  void EnrollUserDefBoundary(UserDefBoundaryFunc);
  void EnrollInternalBoundary(InternalBoundaryFunc);
  void EnrollFluxBoundary(UserDefBoundaryFunc);

  void EnforcePeriodic(int, BoundarySide ); ///< Enforce periodic BC in direction and side
  void EnforceReflective(int, BoundarySide ); ///< Enforce reflective BC in direction and side
  void EnforceOutflow(int, BoundarySide ); ///< Enforce outflow BC in direction and side
  void EnforceShearingBox(real, int, BoundarySide ); ///< Enforce Shearing box BCs

  #ifdef WITH_MPI
  Mpi mpi;                     ///< Mpi object when WITH_MPI is set
  #endif

    // User defined Boundary conditions
  UserDefBoundaryFunc userDefBoundaryFunc{NULL};
  bool haveUserDefBoundary{false};

  // Internal boundary function
  bool haveInternalBoundary{false};
  InternalBoundaryFunc internalBoundaryFunc{NULL};

  // Flux boundary function
  bool haveFluxBoundary{false};
  UserDefBoundaryFunc fluxBoundaryFunc{NULL};

  // specific for loops on ghost cells
  template <typename Function>
  void BoundaryFor(const std::string &,
                            const int &,
                            const BoundarySide &,
                            Function );

  template <typename Function>
  void BoundaryForAll(const std::string &,
                            const int &,
                            const BoundarySide &,
                            Function );

  template <typename Function>
  void BoundaryForX1s(const std::string &,
                            const int &,
                            const BoundarySide &,
                            Function );

  template <typename Function>
  void BoundaryForX2s(const std::string &,
                            const int &,
                            const BoundarySide &,
                            Function );

  template <typename Function>
  void BoundaryForX3s(const std::string &,
                            const int &,
                            const BoundarySide &,
                            Function );
  IdefixArray4D<real> sBArray;    ///< Array use by shearingbox boundary conditions

 private:
  Fluid<Phys> *fluid;    // pointer to parent hydro object
  DataBlock *data;  // pointer to parent datablock
};

#include "fluid.hpp"

template<typename Phys>
Boundary<Phys>::Boundary(Input & input, Grid &grid, Fluid<Phys>* fluid) {
  idfx::pushRegion("HydroBoundary::Init");
  this->fluid = fluid;
  this->data = fluid->data;

  if(data->lbound[IDIR] == shearingbox || data->rbound[IDIR] == shearingbox) {
    // using np_tot[...]+1 points to allow this buffer to represent
    // fields that are defined on faces
    sBArray = IdefixArray4D<real>("ShearingBoxArray",
                                  Phys::nvar,
                                  data->np_tot[KDIR]+1,
                                  data->np_tot[JDIR]+1,
                                  data->nghost[IDIR]);
  }

  // Init MPI stack when needed
#ifdef WITH_MPI
  ////////////////////////////////////////////////////////////////////////////
  // Init variable mappers
  // The variable mapper list all of the variable which are exchanged in MPI boundary calls
  // This is required since we skip some of the variables in Vc to limit the amount of data
  // being exchanged
  int mapNVars = Phys::nvar;
  if constexpr(Phys::mhd) mapNVars -= DIMENSIONS;

  std::vector<int> mapVars;

  // Init the list of variables we will exchange in MPI routines
  int ntarget = 0;
  for(int n = 0 ; n < mapNVars ; n++) {
    mapVars.push_back(ntarget);
    ntarget++;
    if constexpr(Phys::mhd) {
      // Skip centered field components if they are also defined in Vs
      #if DIMENSIONS >= 1
        if(ntarget==BX1) ntarget++;
      #endif
      #if DIMENSIONS >= 2
        if(ntarget==BX2) ntarget++;
      #endif
      #if DIMENSIONS == 3
        if(ntarget==BX3) ntarget++;
      #endif
    }
  }

  mpi.Init(data->mygrid, mapVars, data->nghost.data(), data->np_int.data(), Phys::mhd);

#endif // MPI
  idfx::popRegion();
}

template<typename Phys>
void Boundary<Phys>::EnrollFluxBoundary(UserDefBoundaryFunc myFunc) {
  this->haveFluxBoundary = true;
  this->fluxBoundaryFunc = myFunc;
}

template<typename Phys>
void Boundary<Phys>::EnforceFluxBoundaries(int dir) {
  idfx::pushRegion("HydroBoundary::EnforceFluxBoundaries");
  if(haveFluxBoundary) {
    if(data->lbound[dir] != internal) {
      fluxBoundaryFunc(*data, dir, left, data->t);
    }
    if(data->rbound[dir] != internal) {
      fluxBoundaryFunc(*data, dir, right, data->t);
    }
  } else {
    IDEFIX_ERROR("Cannot enforce flux boundary conditions without enrolling a specific function");
  }
  idfx::popRegion();
}

template<typename Phys>
void Boundary<Phys>::SetBoundaries(real t) {
  idfx::pushRegion("HydroBoundary::SetBoundaries");
  // set internal boundary conditions
  if(haveInternalBoundary) internalBoundaryFunc(*data, t);
  for(int dir=0 ; dir < DIMENSIONS ; dir++ ) {
      // MPI Exchange data when needed
    #ifdef WITH_MPI
    if(data->mygrid->nproc[dir]>1) {
      switch(dir) {
        case 0:
          mpi.ExchangeX1(fluid->Vc, fluid->Vs);
          break;
        case 1:
          mpi.ExchangeX2(fluid->Vc, fluid->Vs);
          break;
        case 2:
          mpi.ExchangeX3(fluid->Vc, fluid->Vs);
          break;
      }
    }
    #endif
    EnforceBoundaryDir(t, dir);
    if constexpr(Phys::mhd) {
      // Reconstruct the normal field component when using CT
      ReconstructNormalField(dir);
    }
  } // Loop on dimension ends

  if constexpr(Phys::mhd) {
    // Remake the cell-centered field.
    ReconstructVcField(fluid->Vc);
  }

  idfx::popRegion();
}


// Enforce boundary conditions by writing into ghost zones
template<typename Phys>
void Boundary<Phys>::EnforceBoundaryDir(real t, int dir) {
  idfx::pushRegion("Hydro::EnforceBoundaryDir");

  // left boundary

  switch(data->lbound[dir]) {
    case internal:
      // internal is used for MPI-enforced boundary conditions. Nothing to be done here.
      break;

    case periodic:
      if(data->mygrid->nproc[dir] > 1) break; // Periodicity already enforced by MPI calls
      EnforcePeriodic(dir,left);
      break;

    case reflective:
      EnforceReflective(dir,left);
      break;

    case outflow:
      EnforceOutflow(dir,left);
      break;

    case shearingbox:
      EnforceShearingBox(t,dir,left);
      break;
    case axis:
      fluid->myAxis->EnforceAxisBoundary(left);
      break;
    case userdef:
      if(this->haveUserDefBoundary)
        this->userDefBoundaryFunc(*data, dir, left, t);
      else
        IDEFIX_ERROR("No function has been enrolled to define your own boundary conditions");
      break;

    default:
      std::stringstream msg ("Boundary condition type is not yet implemented");
      IDEFIX_ERROR(msg);
  }

  // right boundary

  switch(data->rbound[dir]) {
    case internal:
      // internal is used for MPI-enforced boundary conditions. Nothing to be done here.
      break;

    case periodic:
      if(data->mygrid->nproc[dir] > 1) break; // Periodicity already enforced by MPI calls
      EnforcePeriodic(dir,right);
      break;
    case reflective:
      EnforceReflective(dir,right);
      break;
    case outflow:
      EnforceOutflow(dir,right);
      break;
    case shearingbox:
      EnforceShearingBox(t,dir,right);
      break;
    case axis:
      fluid->myAxis->EnforceAxisBoundary(right);
      break;
    case userdef:
      if(this->haveUserDefBoundary)
        this->userDefBoundaryFunc(*data, dir, right, t);
      else
        IDEFIX_ERROR("No function has been enrolled to define your own boundary conditions");
      break;
    default:
      std::stringstream msg("Boundary condition type is not yet implemented");
      IDEFIX_ERROR(msg);
  }

  idfx::popRegion();
}


template<typename Phys>
void Boundary<Phys>::ReconstructVcField(IdefixArray4D<real> &Vc) {
  idfx::pushRegion("Hydro::ReconstructVcField");

  IdefixArray4D<real> Vs=fluid->Vs;

  // Reconstruct cell average field when using CT
  idefix_for("ReconstructVcMagField",0,data->np_tot[KDIR],0,data->np_tot[JDIR],0,data->np_tot[IDIR],
    KOKKOS_LAMBDA (int k, int j, int i) {
      D_EXPAND( Vc(BX1,k,j,i) = HALF_F * (Vs(BX1s,k,j,i) + Vs(BX1s,k,j,i+1)) ;  ,
                Vc(BX2,k,j,i) = HALF_F * (Vs(BX2s,k,j,i) + Vs(BX2s,k,j+1,i)) ;  ,
                Vc(BX3,k,j,i) = HALF_F * (Vs(BX3s,k,j,i) + Vs(BX3s,k+1,j,i)) ;  )
    }
  );

  idfx::popRegion();
}


template<typename Phys>
void Boundary<Phys>::ReconstructNormalField(int dir) {
  idfx::pushRegion("Hydro::ReconstructNormalField");

  const bool reconstructLeft  = !(data->lbound[dir]==periodic || data->lbound[dir]==internal);
  const bool reconstructRight = !(data->rbound[dir]==periodic || data->rbound[dir]==internal);

  // nothing to reconstruct in that direction!
  if((!reconstructLeft) && (!reconstructRight)) return;

  // Reconstruct the field
  IdefixArray4D<real> Vs = fluid->Vs;
  // Coordinates
  IdefixArray1D<real> x1=data->x[IDIR];
  IdefixArray1D<real> x2=data->x[JDIR];
  IdefixArray1D<real> x3=data->x[KDIR];
  IdefixArray1D<real> dx1=data->dx[IDIR];
  IdefixArray1D<real> dx2=data->dx[JDIR];
  IdefixArray1D<real> dx3=data->dx[KDIR];

  IdefixArray3D<real> Ax1=data->A[IDIR];
  IdefixArray3D<real> Ax2=data->A[JDIR];
  IdefixArray3D<real> Ax3=data->A[KDIR];


  // reconstruct BX1s
  int nstart = data->beg[IDIR]-1;
  int nend = data->end[IDIR];

  const int nx1=data->np_tot[IDIR];
  const int nx2=data->np_tot[JDIR];
  const int nx3=data->np_tot[KDIR];

  if(dir==IDIR) {
    idefix_for("ReconstructBX1s",0,nx3,0,nx2,
      KOKKOS_LAMBDA (int k, int j) {
        if(reconstructLeft) {
          for(int i = nstart ; i>=0 ; i-- ) {
            Vs(BX1s,k,j,i) = 1.0 / Ax1(k,j,i) * ( Ax1(k,j,i+1)*Vs(BX1s,k,j,i+1)
                            +(D_EXPAND( ZERO_F                                                 ,
                              + Ax2(k,j+1,i) * Vs(BX2s,k,j+1,i) - Ax2(k,j,i) * Vs(BX2s,k,j,i) ,
                              + Ax3(k+1,j,i) * Vs(BX3s,k+1,j,i) - Ax3(k,j,i) * Vs(BX3s,k,j,i)  )));
          }
        }

        if(reconstructRight) {
          for(int i = nend ; i<nx1 ; i++ ) {
            Vs(BX1s,k,j,i+1) = 1.0 / Ax1(k,j,i+1) * ( Ax1(k,j,i)*Vs(BX1s,k,j,i)
                              -(D_EXPAND(      ZERO_F                                           ,
                              + Ax2(k,j+1,i) * Vs(BX2s,k,j+1,i) - Ax2(k,j,i) * Vs(BX2s,k,j,i)  ,
                              + Ax3(k+1,j,i) * Vs(BX3s,k+1,j,i) - Ax3(k,j,i) * Vs(BX3s,k,j,i)  )));
          }
        }
      }
    );
  }

#if DIMENSIONS >=2
  if(dir==JDIR) {
    nstart = data->beg[JDIR]-1;
    nend = data->end[JDIR];
    if(!fluid->haveAxis) {
      idefix_for("ReconstructBX2s",0,data->np_tot[KDIR],0,data->np_tot[IDIR],
        KOKKOS_LAMBDA (int k, int i) {
          if(reconstructLeft) {
            for(int j = nstart ; j>=0 ; j-- ) {
              Vs(BX2s,k,j,i) = 1.0 / Ax2(k,j,i) * ( Ax2(k,j+1,i)*Vs(BX2s,k,j+1,i)
                      +(D_EXPAND( Ax1(k,j,i+1) * Vs(BX1s,k,j,i+1) - Ax1(k,j,i) * Vs(BX1s,k,j,i)  ,
                                                                                                ,
                            + Ax3(k+1,j,i) * Vs(BX3s,k+1,j,i) - Ax3(k,j,i) * Vs(BX3s,k,j,i) )));
            }
          }
          if(reconstructRight) {
            for(int j = nend ; j<nx2 ; j++ ) {
              Vs(BX2s,k,j+1,i) = 1.0 / Ax2(k,j+1,i) * ( Ax2(k,j,i)*Vs(BX2s,k,j,i)
                       -(D_EXPAND( Ax1(k,j,i+1) * Vs(BX1s,k,j,i+1) - Ax1(k,j,i) * Vs(BX1s,k,j,i)  ,
                                                                                                ,
                            + Ax3(k+1,j,i) * Vs(BX3s,k+1,j,i) - Ax3(k,j,i) * Vs(BX3s,k,j,i) )));
            }
          }
        }
      );
    } else {
      // We have an axis, ask myAxis to do that job for us
      fluid->myAxis->ReconstructBx2s();
    }
  }
#endif

#if DIMENSIONS == 3
  if(dir==KDIR) {
    nstart = data->beg[KDIR]-1;
    nend = data->end[KDIR];

    idefix_for("ReconstructBX3s",0,data->np_tot[JDIR],0,data->np_tot[IDIR],
      KOKKOS_LAMBDA (int j, int i) {
        if(reconstructLeft) {
          for(int k = nstart ; k>=0 ; k-- ) {
            Vs(BX3s,k,j,i) = 1.0 / Ax3(k,j,i) * ( Ax3(k+1,j,i)*Vs(BX3s,k+1,j,i)
                            + ( Ax1(k,j,i+1) * Vs(BX1s,k,j,i+1) - Ax1(k,j,i) * Vs(BX1s,k,j,i)
                            + Ax2(k,j+1,i) * Vs(BX2s,k,j+1,i) - Ax2(k,j,i) * Vs(BX2s,k,j,i) ));
          }
        }
        if(reconstructRight) {
          for(int k = nend ; k<nx3 ; k++ ) {
            Vs(BX3s,k+1,j,i) = 1.0 / Ax3(k+1,j,i) * ( Ax3(k,j,i)*Vs(BX3s,k,j,i)
                              - ( Ax1(k,j,i+1) * Vs(BX1s,k,j,i+1) - Ax1(k,j,i) * Vs(BX1s,k,j,i)
                              +  Ax2(k,j+1,i) * Vs(BX2s,k,j+1,i) - Ax2(k,j,i) * Vs(BX2s,k,j,i) ));
          }
        }
      }
    );
  }
#endif

  idfx::popRegion();
}


template<typename Phys>
void Boundary<Phys>::EnrollUserDefBoundary(UserDefBoundaryFunc myFunc) {
  this->userDefBoundaryFunc = myFunc;
  this->haveUserDefBoundary = true;
}

template<typename Phys>
void Boundary<Phys>::EnrollInternalBoundary(InternalBoundaryFunc myFunc) {
  this->internalBoundaryFunc = myFunc;
  this->haveInternalBoundary = true;
}

template<typename Phys>
void Boundary<Phys>::EnforcePeriodic(int dir, BoundarySide side ) {
  IdefixArray4D<real> Vc = fluid->Vc;
  int nxi = data->np_int[IDIR];
  int nxj = data->np_int[JDIR];
  int nxk = data->np_int[KDIR];

  const int ighost = data->nghost[IDIR];
  const int jghost = data->nghost[JDIR];
  const int kghost = data->nghost[KDIR];

  BoundaryForAll("BoundaryPeriodic", dir, side,
        KOKKOS_LAMBDA (int n, int k, int j, int i) {
          int iref, jref, kref;
          // This hack takes care of cases where we have more ghost zones than active zones
          if(dir==IDIR)
            iref = ighost + (i+ighost*(nxi-1))%nxi;
          else
            iref = i;
          if(dir==JDIR)
            jref = jghost + (j+jghost*(nxj-1))%nxj;
          else
            jref = j;
          if(dir==KDIR)
            kref = kghost + (k+kghost*(nxk-1))%nxk;
          else
            kref = k;

          Vc(n,k,j,i) = Vc(n,kref,jref,iref);
        });

  if constexpr(Phys::mhd) {
    IdefixArray4D<real> Vs = fluid->Vs;
    BoundaryForX1s("BoundaryPeriodicX1s",dir,side,
    KOKKOS_LAMBDA (int k, int j, int i) {
      int iref, jref, kref;
        // This hack takes care of cases where we have more ghost zones than active zones
        if(dir==IDIR)
          iref = ighost + (i+ighost*(nxi-1))%nxi;
        else
          iref = i;
        if(dir==JDIR)
          jref = jghost + (j+jghost*(nxj-1))%nxj;
        else
          jref = j;
        if(dir==KDIR)
          kref = kghost + (k+kghost*(nxk-1))%nxk;
        else
          kref = k;

        Vs(BX1s,k,j,i) = Vs(BX1s,kref,jref,iref);
    });
    #if DIMENSIONS >=2
      BoundaryForX2s("BoundaryPeriodicX2s",dir,side,
      KOKKOS_LAMBDA (int k, int j, int i) {
        int iref, jref, kref;
          // This hack takes care of cases where we have more ghost zones than active zones
          if(dir==IDIR)
            iref = ighost + (i+ighost*(nxi-1))%nxi;
          else
            iref = i;
          if(dir==JDIR)
            jref = jghost + (j+jghost*(nxj-1))%nxj;
          else
            jref = j;
          if(dir==KDIR)
            kref = kghost + (k+kghost*(nxk-1))%nxk;
          else
            kref = k;

          Vs(BX2s,k,j,i) = Vs(BX2s,kref,jref,iref);
      });
    #endif
    #if DIMENSIONS == 3
      BoundaryForX3s("BoundaryPeriodicX3s",dir,side,
      KOKKOS_LAMBDA (int k, int j, int i) {
        int iref, jref, kref;
          // This hack takes care of cases where we have more ghost zones than active zones
          if(dir==IDIR)
            iref = ighost + (i+ighost*(nxi-1))%nxi;
          else
            iref = i;
          if(dir==JDIR)
            jref = jghost + (j+jghost*(nxj-1))%nxj;
          else
            jref = j;
          if(dir==KDIR)
            kref = kghost + (k+kghost*(nxk-1))%nxk;
          else
            kref = k;

          Vs(BX3s,k,j,i) = Vs(BX3s,kref,jref,iref);
      });
    #endif
  }// MHD
}


template<typename Phys>
void Boundary<Phys>::EnforceReflective(int dir, BoundarySide side ) {
  IdefixArray4D<real> Vc = fluid->Vc;
  const int nxi = data->np_int[IDIR];
  const int nxj = data->np_int[JDIR];
  const int nxk = data->np_int[KDIR];

  const int ighost = data->nghost[IDIR];
  const int jghost = data->nghost[JDIR];
  const int kghost = data->nghost[KDIR];

  BoundaryForAll("BoundaryReflective", dir, side,
        KOKKOS_LAMBDA (int n, int k, int j, int i) {
          // ref= 2*ibound -i -1
          // with ibound = nghost on the left and ibount = nghost + nx -1 on the right
          const int iref = (dir==IDIR) ? 2*(ighost + side*(nxi-1)) - i - 1 : i;
          const int jref = (dir==JDIR) ? 2*(jghost + side*(nxj-1)) - j - 1 : j;
          const int kref = (dir==KDIR) ? 2*(kghost + side*(nxk-1)) - k - 1 : k;

          const int sign = (n == VX1+dir) ? -1.0 : 1.0;

          Vc(n,k,j,i) = sign * Vc(n,kref,jref,iref);
        });

  if constexpr(Phys::mhd) {
    IdefixArray4D<real> Vs = fluid->Vs;
    if(dir==JDIR || dir==KDIR) {
      BoundaryForX1s("BoundaryReflectiveX1s",dir,side,
        KOKKOS_LAMBDA (int k, int j, int i) {
          // ref= 2*ibound -i -1
          // with ibound = nghost on the left and ibount = nghost + nx -1 on the right
          //const int iref = (dir==IDIR) ? 2*(ighost + side*(nxi-1)) - i - 1 : i;
          const int jref = (dir==JDIR) ? 2*(jghost + side*(nxj-1)) - j - 1 : j;
          const int kref = (dir==KDIR) ? 2*(kghost + side*(nxk-1)) - k - 1 : k;

          Vs(BX1s,k,j,i) = -Vs(BX1s,kref,jref,i);
        });
    }
    #if DIMENSIONS >=2
      if(dir==IDIR || dir==KDIR) {
        BoundaryForX2s("BoundaryReflectiveX2s",dir,side,
          KOKKOS_LAMBDA (int k, int j, int i) {
            const int iref = (dir==IDIR) ? 2*(ighost + side*(nxi-1)) - i - 1 : i;
            //const int jref = (dir==JDIR) ? 2*(jghost + side*(nxj-1)) - j - 1 : j;
            const int kref = (dir==KDIR) ? 2*(kghost + side*(nxk-1)) - k - 1 : k;

              Vs(BX2s,k,j,i) = -Vs(BX2s,kref,j,iref);
          });
      }
    #endif
    #if DIMENSIONS == 3
      if(dir==IDIR || dir==JDIR) {
        BoundaryForX3s("BoundaryReflectiveX3s",dir,side,
          KOKKOS_LAMBDA (int k, int j, int i) {
            const int iref = (dir==IDIR) ? 2*(ighost + side*(nxi-1)) - i - 1 : i;
            const int jref = (dir==JDIR) ? 2*(jghost + side*(nxj-1)) - j - 1 : j;
            //const int kref = (dir==KDIR) ? 2*(kghost + side*(nxk-1)) - k - 1 : k;

            Vs(BX3s,k,j,i) = -Vs(BX3s,k,jref,iref);
          });
      }
    #endif
  }// MHD
}

template<typename Phys>
void Boundary<Phys>::EnforceOutflow(int dir, BoundarySide side ) {
  IdefixArray4D<real> Vc = fluid->Vc;
  const int nxi = data->np_int[IDIR];
  const int nxj = data->np_int[JDIR];
  const int nxk = data->np_int[KDIR];

  const int ighost = data->nghost[IDIR];
  const int jghost = data->nghost[JDIR];
  const int kghost = data->nghost[KDIR];

  BoundaryForAll("BoundaryOutflow", dir, side,
        KOKKOS_LAMBDA (int n, int k, int j, int i) {
          // ref= ibound
          // with ibound = nghost on the left and ibound = nghost + nx -1 on the right
          const int iref = (dir==IDIR) ? ighost + side*(nxi-1) : i;
          const int jref = (dir==JDIR) ? jghost + side*(nxj-1) : j;
          const int kref = (dir==KDIR) ? kghost + side*(nxk-1) : k;

          // should it go inwards or outwards?
          // side = 1 on the left and =-1 on the right
          const int sign = 1-2*side;

          if( (n== VX1+dir) && (sign*Vc(n,kref,jref,iref) >= ZERO_F) ) {
            Vc(n,k,j,i) = ZERO_F;
          } else {
            Vc(n,k,j,i) = Vc(n,kref,jref,iref);
          }
        });

  if constexpr(Phys::mhd) {
    IdefixArray4D<real> Vs = fluid->Vs;
    if(dir==JDIR || dir==KDIR) {
      BoundaryForX1s("BoundaryOutflowX1s",dir,side,
        KOKKOS_LAMBDA (int k, int j, int i) {
          // with ibound = nghost on the left and ibount = nghost + nx -1 on the right
          //const int iref = (dir==IDIR) ? ighost + side*(nxi-1) : i;
          const int jref = (dir==JDIR) ? jghost + side*(nxj-1) : j;
          const int kref = (dir==KDIR) ? kghost + side*(nxk-1) : k;

          Vs(BX1s,k,j,i) = Vs(BX1s,kref,jref,i);
        });
    }
    #if DIMENSIONS >=2
      if(dir==IDIR || dir==KDIR) {
        BoundaryForX2s("BoundaryOutflowX2s",dir,side,
          KOKKOS_LAMBDA (int k, int j, int i) {
            const int iref = (dir==IDIR) ? ighost + side*(nxi-1) : i;
            //const int jref = (dir==JDIR) ? jghost + side*(nxj-1) : j;
            const int kref = (dir==KDIR) ? kghost + side*(nxk-1) : k;

            Vs(BX2s,k,j,i) = Vs(BX2s,kref,j,iref);
          });
      }
    #endif
    #if DIMENSIONS == 3
      if(dir==IDIR || dir==JDIR) {
        BoundaryForX3s("BoundaryOutflowX3s",dir,side,
          KOKKOS_LAMBDA (int k, int j, int i) {
            const int iref = (dir==IDIR) ? ighost + side*(nxi-1) : i;
            const int jref = (dir==JDIR) ? jghost + side*(nxj-1) : j;
            //const int kref = (dir==KDIR) ? kghost + side*(nxk-1) : k;

            Vs(BX3s,k,j,i) = Vs(BX3s,k,jref,iref);
          });
      }
    #endif
  }// MHD
}

template<typename Phys>
void Boundary<Phys>::EnforceShearingBox(real t, int dir, BoundarySide side) {
  if(dir != IDIR)
    IDEFIX_ERROR("Shearing box boundaries can only be applied along the X1 direction");
  if(data->mygrid->nproc[JDIR]>1)
    IDEFIX_ERROR("Shearing box is not yet compatible with domain decomposition in X2");

  // First thing is to enforce periodicity (already performed by MPI)
  if(data->mygrid->nproc[dir] == 1) EnforcePeriodic(dir, side);

  IdefixArray4D<real> scrh = sBArray;
  IdefixArray4D<real> Vc = fluid->Vc;

  const int nxi = data->np_int[IDIR];
  const int nxj = data->np_int[JDIR];

  const int ighost = data->nghost[IDIR];
  const int jghost = data->nghost[JDIR];

  // Where does the boundary starts along x1?
  const int istart = side*(ighost+nxi);

  // Shear rate
  const real S  = fluid->sbS;

  // Box size
  const real Lx = data->mygrid->xend[IDIR] - data->mygrid->xbeg[IDIR];
  const real Ly = data->mygrid->xend[JDIR] - data->mygrid->xbeg[JDIR];

  // total number of cells in y (active domain)
  const int ny = data->mygrid->np_int[JDIR];
  const real dy = Ly/ny;

  // Compute offset in y modulo the box size
  const int sign=2*side-1;
  const real sbVelocity = sign*S*Lx;
  real dL = std::fmod(sbVelocity*t,Ly);

  // translate this into # of cells
  const int m = static_cast<int> (std::floor(dL/dy+HALF_F));

  // remainding shift
  const real eps = dL / dy - m;


  // Now we need to perform the shift
  BoundaryForAll("BoundaryShearingBox", dir, side,
        KOKKOS_LAMBDA ( int n, int k, int j, int i) {
          // jorigin
          const int jo = jghost + ((j-m-jghost)%nxj+nxj)%nxj;
          const int jop2 = jghost + ((jo+2-jghost)%nxj+nxj)%nxj;
          const int jop1 = jghost + ((jo+1-jghost)%nxj+nxj)%nxj;
          const int jom1 = jghost + ((jo-1-jghost)%nxj+nxj)%nxj;
          const int jom2 = jghost + ((jo-2-jghost)%nxj+nxj)%nxj;

          // Define Left and right fluxes
          // Fluxes are defined from slope-limited interpolation
          // Using Van-leer slope limiter (consistently with the main advection scheme)
          real Fl,Fr;
          real dqm, dqp, dq;

          if(eps>=ZERO_F) {
            // Compute Fl
            dqm = Vc(n,k,jom1,i) - Vc(n,k,jom2,i);
            dqp = Vc(n,k,jo,i) - Vc(n,k,jom1,i);
            dq = (dqp*dqm > ZERO_F ? TWO_F*dqp*dqm/(dqp + dqm) : ZERO_F);

            Fl = Vc(n,k,jom1,i) + 0.5*dq*(1.0-eps);
            //Compute Fr
            dqm=dqp;
            dqp = Vc(n,k,jop1,i) - Vc(n,k,jo,i);
            dq = (dqp*dqm > ZERO_F ? TWO_F*dqp*dqm/(dqp + dqm) : ZERO_F);

            Fr = Vc(n,k,jo,i) + 0.5*dq*(1.0-eps);
          } else {
            //Compute Fl
            dqm = Vc(n,k,jo,i) - Vc(n,k,jom1,i);
            dqp = Vc(n,k,jop1,i) - Vc(n,k,jo,i);
            dq = (dqp*dqm > ZERO_F ? TWO_F*dqp*dqm/(dqp + dqm) : ZERO_F);

            Fl = Vc(n,k,jo,i) - 0.5*dq*(1.0+eps);
            // Compute Fr
            dqm=dqp;
            dqp = Vc(n,k,jop2,i) - Vc(n,k,jop1,i);
            dq = (dqp*dqm > ZERO_F ? TWO_F*dqp*dqm/(dqp + dqm) : ZERO_F);

            Fr = Vc(n,k,jop1,i) - 0.5*dq*(1.0+eps);
          }
          scrh(n,k,j,i-istart) = Vc(n,k,jo,i) - eps*(Fr - Fl);
        });
  // Copy scrach back to our boundary
  BoundaryForAll("BoundaryShearingBoxCopy", dir, side,
        KOKKOS_LAMBDA ( int n, int k, int j, int i) {
          Vc(n,k,j,i) = scrh(n,k,j,i-istart);
          if(n==VX2) Vc(n,k,j,i) += sbVelocity;
        });

  // Magnetised version of the same thing
  if constexpr(Phys::mhd) {
    IdefixArray4D<real> Vs = fluid->Vs;
    #if DIMENSIONS >= 2
      for(int component = BX2s ; component < DIMENSIONS ; component++) {
        BoundaryFor("BoundaryShearingBoxBXs", dir, side,
        KOKKOS_LAMBDA (int k, int j, int i) {
          // jorigin
          const int jo = jghost + ((j-m-jghost)%nxj+nxj)%nxj;
          const int jop2 = jghost + ((jo+2-jghost)%nxj+nxj)%nxj;
          const int jop1 = jghost + ((jo+1-jghost)%nxj+nxj)%nxj;
          const int jom1 = jghost + ((jo-1-jghost)%nxj+nxj)%nxj;
          const int jom2 = jghost + ((jo-2-jghost)%nxj+nxj)%nxj;

          // Define Left and right fluxes
          // Fluxes are defined from slope-limited interpolation
          // Using Van-leer slope limiter (consistently with the main advection scheme)
          real Fl,Fr;
          real dqm, dqp, dq;

          if(eps>=ZERO_F) {
            // Compute Fl
            dqm = Vs(component,k,jom1,i) - Vs(component,k,jom2,i);
            dqp = Vs(component,k,jo,i) - Vs(component,k,jom1,i);
            dq = (dqp*dqm > ZERO_F ? TWO_F*dqp*dqm/(dqp + dqm) : ZERO_F);

            Fl = Vs(component,k,jom1,i) + 0.5*dq*(1.0-eps);
            //Compute Fr
            dqm=dqp;
            dqp = Vs(component,k,jop1,i) - Vs(component,k,jo,i);
            dq = (dqp*dqm > ZERO_F ? TWO_F*dqp*dqm/(dqp + dqm) : ZERO_F);

            Fr = Vs(component,k,jo,i) + 0.5*dq*(1.0-eps);
          } else {
            //Compute Fl
            dqm = Vs(component,k,jo,i) - Vs(component,k,jom1,i);
            dqp = Vs(component,k,jop1,i) - Vs(component,k,jo,i);
            dq = (dqp*dqm > ZERO_F ? TWO_F*dqp*dqm/(dqp + dqm) : ZERO_F);

            Fl = Vs(component,k,jo,i) - 0.5*dq*(1.0+eps);
            // Compute Fr
            dqm=dqp;
            dqp = Vs(component,k,jop2,i) - Vs(component,k,jop1,i);
            dq = (dqp*dqm > ZERO_F ? TWO_F*dqp*dqm/(dqp + dqm) : ZERO_F);

            Fr = Vs(component,k,jop1,i) - 0.5*dq*(1.0+eps);
          }
          scrh(0,k,j,i-istart) = Vs(component,k,jo,i) - eps*(Fr - Fl);
        });
        // Copy scratch back to our boundary
        BoundaryFor("BoundaryShearingBoxCopyBXs", dir, side,
              KOKKOS_LAMBDA ( int k, int j, int i) {
                Vs(component,k,j,i) = scrh(0,k,j,i-istart);
              });
      }// loop on components
    #endif// DIMENSIONS
  } // MHD
}

template<typename Phys>
template <typename Function>
inline void Boundary<Phys>::BoundaryForAll(
  const std::string & name,
  const int &dir,
  const BoundarySide &side,
  Function function) {
    const int nxi = data->np_int[IDIR];
    const int nxj = data->np_int[JDIR];
    const int nxk = data->np_int[KDIR];

    const int ighost = data->nghost[IDIR];
    const int jghost = data->nghost[JDIR];
    const int kghost = data->nghost[KDIR];

    // Boundaries of the loop
    const int ibeg = (dir == IDIR) ? side*(ighost+nxi) : 0;
    const int iend = (dir == IDIR) ? ighost + side*(ighost+nxi) : data->np_tot[IDIR];
    const int jbeg = (dir == JDIR) ? side*(jghost+nxj) : 0;
    const int jend = (dir == JDIR) ? jghost + side*(jghost+nxj) : data->np_tot[JDIR];
    const int kbeg = (dir == KDIR) ? side*(kghost+nxk) : 0;
    const int kend = (dir == KDIR) ? kghost + side*(kghost+nxk) : data->np_tot[KDIR];

    idefix_for(name, 0, Phys::nvar, kbeg, kend, jbeg, jend, ibeg, iend, function);
}

template<typename Phys>
template <typename Function>
inline void Boundary<Phys>::BoundaryFor(
  const std::string & name,
  const int &dir,
  const BoundarySide &side,
  Function function) {
    const int nxi = data->np_int[IDIR];
    const int nxj = data->np_int[JDIR];
    const int nxk = data->np_int[KDIR];

    const int ighost = data->nghost[IDIR];
    const int jghost = data->nghost[JDIR];
    const int kghost = data->nghost[KDIR];

    // Boundaries of the loop
    const int ibeg = (dir == IDIR) ? side*(ighost+nxi) : 0;
    const int iend = (dir == IDIR) ? ighost + side*(ighost+nxi) : data->np_tot[IDIR];
    const int jbeg = (dir == JDIR) ? side*(jghost+nxj) : 0;
    const int jend = (dir == JDIR) ? jghost + side*(jghost+nxj) : data->np_tot[JDIR];
    const int kbeg = (dir == KDIR) ? side*(kghost+nxk) : 0;
    const int kend = (dir == KDIR) ? kghost + side*(kghost+nxk) : data->np_tot[KDIR];



    idefix_for(name, kbeg, kend, jbeg, jend, ibeg, iend, function);
}

template<typename Phys>
template <typename Function>
inline void Boundary<Phys>::BoundaryForX1s(
  const std::string & name,
  const int &dir,
  const BoundarySide &side,
  Function function) {
    const int nxi = data->np_int[IDIR]+1;
    const int nxj = data->np_int[JDIR];
    const int nxk = data->np_int[KDIR];

    const int ighost = data->nghost[IDIR];
    const int jghost = data->nghost[JDIR];
    const int kghost = data->nghost[KDIR];

    // Boundaries of the loop
    const int ibeg = (dir == IDIR) ? side*(ighost+nxi) : 0;
    const int iend = (dir == IDIR) ? ighost + side*(ighost+nxi) : data->np_tot[IDIR]+1;
    const int jbeg = (dir == JDIR) ? side*(jghost+nxj) : 0;
    const int jend = (dir == JDIR) ? jghost + side*(jghost+nxj) : data->np_tot[JDIR];
    const int kbeg = (dir == KDIR) ? side*(kghost+nxk) : 0;
    const int kend = (dir == KDIR) ? kghost + side*(kghost+nxk) : data->np_tot[KDIR];

    idefix_for(name, kbeg, kend, jbeg, jend, ibeg, iend, function);
}

template<typename Phys>
template <typename Function>
inline void Boundary<Phys>::BoundaryForX2s(
  const std::string & name,
  const int &dir,
  const BoundarySide &side,
  Function function) {
    const int nxi = data->np_int[IDIR];
    const int nxj = data->np_int[JDIR]+1;
    const int nxk = data->np_int[KDIR];

    const int ighost = data->nghost[IDIR];
    const int jghost = data->nghost[JDIR];
    const int kghost = data->nghost[KDIR];

    // Boundaries of the loop
    const int ibeg = (dir == IDIR) ? side*(ighost+nxi) : 0;
    const int iend = (dir == IDIR) ? ighost + side*(ighost+nxi) : data->np_tot[IDIR];
    const int jbeg = (dir == JDIR) ? side*(jghost+nxj) : 0;
    const int jend = (dir == JDIR) ? jghost + side*(jghost+nxj) : data->np_tot[JDIR]+1;
    const int kbeg = (dir == KDIR) ? side*(kghost+nxk) : 0;
    const int kend = (dir == KDIR) ? kghost + side*(kghost+nxk) : data->np_tot[KDIR];

    idefix_for(name, kbeg, kend, jbeg, jend, ibeg, iend, function);
}

template<typename Phys>
template <typename Function>
inline void Boundary<Phys>::BoundaryForX3s(
  const std::string & name,
  const int &dir,
  const BoundarySide &side,
  Function function) {
    const int nxi = data->np_int[IDIR];
    const int nxj = data->np_int[JDIR];
    const int nxk = data->np_int[KDIR]+1;

    const int ighost = data->nghost[IDIR];
    const int jghost = data->nghost[JDIR];
    const int kghost = data->nghost[KDIR];

    // Boundaries of the loop
    const int ibeg = (dir == IDIR) ? side*(ighost+nxi) : 0;
    const int iend = (dir == IDIR) ? ighost + side*(ighost+nxi) : data->np_tot[IDIR];
    const int jbeg = (dir == JDIR) ? side*(jghost+nxj) : 0;
    const int jend = (dir == JDIR) ? jghost + side*(jghost+nxj) : data->np_tot[JDIR];
    const int kbeg = (dir == KDIR) ? side*(kghost+nxk) : 0;
    const int kend = (dir == KDIR) ? kghost + side*(kghost+nxk) : data->np_tot[KDIR]+1;

    idefix_for(name, kbeg, kend, jbeg, jend, ibeg, iend, function);
}


#endif // FLUID_BOUNDARY_BOUNDARY_HPP_
