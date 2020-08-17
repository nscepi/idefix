#include "../idefix.hpp"

// Check if current datablock has nans

int DataBlock::CheckNan()  {
    int nanVs=0;
    int nanVc=0;

    idfx::pushRegion("DataBlock::CheckNan");
    IdefixArray4D<real> Vc=this->Vc;
    
    Kokkos::parallel_reduce("checkNanVc",
                                Kokkos::MDRangePolicy<Kokkos::Rank<4, Kokkos::Iterate::Right, Kokkos::Iterate::Right>>
                                ({0,beg[KDIR],beg[JDIR],beg[IDIR]},{NVAR,end[KDIR], end[JDIR], end[IDIR]}),
                                KOKKOS_LAMBDA (int n, int k, int j, int i, int &nnan) {

                if(std::isnan(Vc(n,k,j,i))) nnan++;
            }, nanVc);

    
    #if MHD == YES
    IdefixArray4D<real> Vs=this->Vs;
    Kokkos::parallel_reduce("checkNanVs",
                                Kokkos::MDRangePolicy<Kokkos::Rank<4, Kokkos::Iterate::Right, Kokkos::Iterate::Right>>
                                ({0,beg[KDIR],beg[JDIR],beg[IDIR]},{DIMENSIONS,end[KDIR]+KOFFSET, end[JDIR]+JOFFSET, end[IDIR]+IOFFSET}),
                                KOKKOS_LAMBDA (int n, int k, int j, int i, int &nnan) {

                if(std::isnan(Vs(n,k,j,i))) nnan++;
            }, nanVs);
    #endif
    if(nanVc+nanVs>0) {
        std::cout << "DataBlock:: rank " << idfx::prank << " found " << nanVc << " Nans in the current datablock. Details will be in corresponding process log file." << std::endl;
        // We need to make copies to find exactly where the thing is wrong
        DataBlockHost dataHost(*this);
        dataHost.SyncFromDevice();

        for(int n = 0 ; n < NVAR ; n ++) {
            for(int k = beg[KDIR] ; k < end[KDIR] ; k++) {
                for(int j = beg[JDIR] ; j < end[JDIR] ; j++) {
                    for(int i = beg[IDIR] ; i < end[IDIR] ; i++) {
                        if(std::isnan(dataHost.Vc(n,k,j,i))) {
                            idfx::cout << "rank " << idfx::prank << ": Nan found  in variable " << this->VcName[n] << std::endl;
                            idfx::cout << "      global (i,j,k) = (" << i-beg[IDIR]+gbeg[IDIR]-nghost[IDIR] << ", " << j-beg[JDIR]+gbeg[JDIR]-nghost[JDIR] << ", " << k-beg[KDIR]+gbeg[KDIR]-nghost[KDIR] << ")" << std::endl;
                            idfx::cout << "      global (x,y,z) = (" << dataHost.x[IDIR](i) << ", " <<  dataHost.x[JDIR](j) << ", " << dataHost.x[KDIR](k) << ")" << std::endl;
                        }
                    }
                }
            }
        }

        #if MHD == YES
            for(int n = 0 ; n < DIMENSIONS ; n ++) {
                for(int k = beg[KDIR] ; k < end[KDIR]+KOFFSET ; k++) {
                    for(int j = beg[JDIR] ; j < end[JDIR]+JOFFSET ; j++) {
                        for(int i = beg[IDIR] ; i < end[IDIR]+IOFFSET ; i++) {
                            if(std::isnan(dataHost.Vs(n,k,j,i))) {
                                idfx::cout << "rank " << idfx::prank << ": Nan found  in variable " << this->VsName[n] << std::endl;
                                idfx::cout << "      global (i,j,k) = (" << i-beg[IDIR]+gbeg[IDIR]-nghost[IDIR] << ", " << j-beg[JDIR]+gbeg[JDIR]-nghost[JDIR] << ", " << k-beg[KDIR]+gbeg[KDIR]-nghost[KDIR] << ")" << std::endl;
                                idfx::cout << "      global (x,y,z) = (" << dataHost.x[IDIR](i) << ", " <<  dataHost.x[JDIR](j) << ", " << dataHost.x[KDIR](k) << ")" << std::endl;
                            }
                        }
                    }
                }
            }
        #endif
    }

    idfx::popRegion();
    return(nanVc+nanVs);
}

    