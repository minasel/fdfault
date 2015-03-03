#include <iostream>
#include <cmath>
#include <cassert>
#include <string>
#include "block.hpp"
#include "boundary.hpp"
#include "cartesian.hpp"
#include "coord.hpp"
#include "fd.hpp"
#include "fields.hpp"
#include "material.hpp"
#include "surface.hpp"
#include <mpi.h>

using namespace std;

block::block(const int ndim_in, const int mode_in, const int nx_in[3], const int xm_in[3], const double x_in[3],
             const double l_in[3], cartesian& cart, fields& f, fd_type& fd) {
    // constructor, no default constructor due to necessary memory allocation
    
	assert(ndim_in == 2 || ndim_in == 3);
    assert(mode_in == 2 || mode_in == 3);
	for (int i=0; i<ndim_in; i++) {
		assert(nx_in[i] > 0);
		assert(xm_in[i] >= 0);
	}
	
	ndim = ndim_in;
    mode = mode_in;
	nbound = ndim*2;
	
	// set block coordinates
	
	for (int i=0; i<ndim; i++) {
		c.set_nx(i,nx_in[i]);
		c.set_xm(i,xm_in[i]);
	}
	
    // set block coordinates to appropriate values
    
    calc_process_info(cart,fd.get_sbporder());
    
    // set material properties
    
    mat.set_lambda(1.);
    mat.set_rho(1.);
    mat.set_g(1.);
    
    // if process has data, allocate grid, fields, and boundaries
    
    if (no_data) { return; }
        
    // set up index info for fields
        
    nxd[0] = cart.get_nx_tot(0)*cart.get_nx_tot(1)*cart.get_nx_tot(2);
    nxd[1] = cart.get_nx_tot(1)*cart.get_nx_tot(2);
    nxd[2] = cart.get_nx_tot(2);
        
    for (int i=0; i<3; i++) {
        mlb[i] = c.get_xm_loc(i)-cart.get_xm_loc(i)+cart.get_xm_ghost(i);
        if (c.get_xm_loc(i) == c.get_xm(i) && c.get_nx(i) > 1) {
            mc[i] = mlb[i]+2*(fd.get_sbporder()-1);
        } else {
            mc[i] = mlb[i];
        }
        if ((c.get_xp_loc(i) == c.get_xp(i)) && c.get_nx(i) > 1) {
            mrb[i] = mlb[i]+c.get_nx_loc(i)-2*(fd.get_sbporder()-1);
            prb[i] = mrb[i]+2*(fd.get_sbporder()-1);
        } else {
            mrb[i] = mlb[i]+c.get_nx_loc(i);
            prb[i] = mrb[i];
        }
    }

    // create boundary surfaces

    double x[6][3];
    double l[6][2];

    for (int i=0; i<ndim; i++) {
        x[0][i] = x_in[i];
        x[2][i] = x_in[i];
        x[4][i] = x_in[i];
    }

    x[1][0] = x_in[0]+l_in[0];
    x[1][1] = x_in[1];
    x[1][2] = x_in[2];
    x[3][0] = x_in[0];
    x[3][1] = x_in[1]+l_in[1];
    x[3][2] = x_in[2];
    x[5][0] = x_in[0];
    x[5][1] = x_in[1];
    x[5][2] = x_in[2]+l_in[2];

    l[0][0] = l_in[1];
    l[0][1] = l_in[2];
    l[1][0] = l_in[1];
    l[1][1] = l_in[2];
    l[2][0] = l_in[0];
    l[2][1] = l_in[2];
    l[3][0] = l_in[0];
    l[3][1] = l_in[2];
    l[4][0] = l_in[0];
    l[4][1] = l_in[1];
    l[5][0] = l_in[0];
    l[5][1] = l_in[1];
        
    // create surfaces
    // surfaces must be global for constructing grid

    surface** surf;

    surf = new surface* [nbound];

    for (int i=0; i<nbound; i++) {
        surf[i] = new surface(ndim,c,i/2,pow(-1.,i+1),x[i],l[i],false);
    }

    // check if surface edges match

    int surf1[12] = {0,0,1,1,0,0,1,1,2,2,3,3};
    int surf2[12] = {2,3,2,3,4,5,4,5,4,5,4,5};
    int edge1[12] = {0,2,0,2,1,3,1,3,1,3,1,3};
    int edge2[12] = {1,0,2,2,0,0,2,2,1,1,3,3};
 
    for (int i=0; i<0; i++) {
        if (!surf[surf1[i]]->has_same_edge(edge1[i],edge2[i],*(surf[surf2[i]]))) {
            std::cerr << "Surface edges do not match in block.cpp\n";
            MPI_Abort(MPI_COMM_WORLD,-1);
        }
    }

    // construct grid

    set_grid(surf,f,cart,fd);

    // deallocate surfaces

    for (int i=0; i<nbound; i++) {
        delete surf[i];
    }

    delete[] surf;

    // allocate boundaries
    
    // create local surfaces to create boundaries
    
    surf = new surface* [nbound];
    
    for (int i=0; i<nbound; i++) {
        surf[i] = new surface(ndim,c,i/2,pow(-1.,i+1),x[i],l[i],true);
    }

    string boundtype[6];
    
    for (int i=0; i<nbound; i++) {
        boundtype[i] = "absorbing";
    }
    
    bound = new boundary* [nbound];

    for (int i=0; i<nbound; i++) {
        bound[i] = new boundary(ndim, mode, i, boundtype[i], c, dx, *(surf[i]), f, mat, cart, fd);
    }
    
    for (int i=0; i<nbound; i++) {
        delete surf[i];
    }
    
    delete[] surf;
    
    init_fields(f);

}
    
block::~block() {
    // destructor, deallocates memory
    
    if (no_data) { return; }
	
    for (int i=0; i<nbound; i++) {
        delete bound[i];
    }
	
    delete[] bound;
    
}

int block::get_nx(const int index) const {
    // returns number of x grid points
    assert(index >= 0 && index < ndim);
    return c.get_nx(index);
}

int block::get_nx_loc(const int index) const {
    // returns number of x grid points for local process
    assert(index >= 0 && index < ndim);
    return c.get_nx_loc(index);
}

int block::get_xm(const int index) const {
    // returns minimum x index
    assert(index >= 0 && index < ndim);
    return c.get_xm(index);
}

int block::get_xm_loc(const int index) const {
    // returns minimum x index for local process
    assert(index >= 0 && index < ndim);
    return c.get_xm_loc(index);
}

int block::get_xp(const int index) const {
    // returns maximum x index
    assert(index >= 0 && index < ndim);
    return c.get_xp(index);
}

int block::get_xp_loc(const int index) const {
    // returns maximum x index for local process
    assert(index >= 0 && index < ndim);
    return c.get_xp_loc(index);
}

double block::get_cp() const {
    // returns block p-wave speed
    return mat.get_cp();
}

double block::get_cs() const {
    // returns block s-wave speed
    return mat.get_cs();
}

double block::get_zp() const {
    // returns block compressional impedance
    return mat.get_zp();
}

double block::get_zs() const {
    // returns block shear impedance
    return mat.get_zs();
}

double block::get_dx(const int index) const {
    // returns grid spacing on transformed grid for index
    assert(index >=0 && index < 3);
    
    return dx[index];
}

void block::calc_df(const double dt, fields& f, fd_type& fd) {
    // does first part of a low storage time step
    
    if (no_data) { return; }
        
    switch (ndim) {
        case 3:
            calc_df_3d(dt,f,fd);
            break;
        case 2:
            switch (mode) {
                case 2:
                    calc_df_mode2(dt,f,fd);
                    break;
                case 3:
                    calc_df_mode3(dt,f,fd);
            }
    }
    
}

void block::set_boundaries(const double dt, fields& f) {
    // applies boundary conditions to block
    
    // only proceed if this process contains data
    
    if (no_data) { return; }

    for (int i=0; i<nbound; i++) {
        bound[i]->apply_bcs(dt,f);
    }

}

/*void block::calc_plastic(const double dt) {
    // calculates final value of fields based on plastic deformation
    
    for (int i=0; i<nx; i++) {
        for (int j=0; j<ny; j++) {
            for (int k=0; k<nz; k++) {
                vx[i][j][k] = 0.;
                vy[i][j][k] = 0.;
                vz[i][j][k] = 0.;
                sxx[i][j][k] = 0.;
                syy[i][j][k] = 0.;
                szz[i][j][k] = 0.;
                sxy[i][j][k] = 0.;
                sxz[i][j][k] = 0.;
                syz[i][j][k] = 0.;
            }
        }
    }
}*/

void block::calc_process_info(cartesian& cart, const int sbporder) {
    // calculate local process-specific information

    // store values to save some typing

    int xm_loc_d[3], nx_loc_d[3];

    int xm[3], xp[3], nx[3];

	for (int i=0; i<ndim; i++) {
		xm_loc_d[i] = cart.get_xm_loc(i);
		nx_loc_d[i] = cart.get_nx_loc(i);
		nx[i] = c.get_nx(i);
		xm[i] = c.get_xm(i);
		xp[i] = c.get_xp(i);
	}
	
    // determine number of local points in block for allocating data
	
	for (int i=0; i<ndim; i++) {
		if (xm_loc_d[i] <= xm[i] && xm_loc_d[i]+nx_loc_d[i]-1 >= xp[i]) {
			c.set_nx_loc(i,nx[i]);
			c.set_xm_loc(i,xm[i]);
		} else if (xm_loc_d[i] > xm[i] && xm_loc_d[i] <= xp[i] && xm_loc_d[i]+nx_loc_d[i]-1 >= xp[i]) {
			c.set_nx_loc(i,xp[i]-xm_loc_d[i]+1);
			c.set_xm_loc(i,xm_loc_d[i]);
		} else if (xm_loc_d[i] <= xm[i] && xm_loc_d[i]+nx_loc_d[i]-1 < xp[i] && xm_loc_d[i]+nx_loc_d[i]-1 >= xm[i]) {
			c.set_nx_loc(i,xm_loc_d[i]+nx_loc_d[i]-xm[i]);
			c.set_xm_loc(i,xm[i]);
		} else if (xm_loc_d[i] > xm[i] && xm_loc_d[i]+nx_loc_d[i]-1 < xp[i]) {
			c.set_nx_loc(i,nx_loc_d[i]);
			c.set_xm_loc(i,xm_loc_d[i]);
		} else {
			c.set_nx_loc(i,0);
		}
	}
    
    // set number of ghost cells
    
	for (int i=0; i<ndim; i++) {
		if (xm_loc_d[i] > xm[i] && xm_loc_d[i] < xp[i]) {
			c.set_xm_ghost(i,sbporder-1);
		} else if (xm_loc_d[i] == xp[i]+1) {
			c.set_xp_ghost(i,1);
		}
    
		if (xm_loc_d[i]+nx_loc_d[i]-1 > xm[i] && xm_loc_d[i]+nx_loc_d[i]-1 < xp[i]) {
			c.set_xp_ghost(i,sbporder-1);
		} else if (xm_loc_d[i]+nx_loc_d[i]-1 == xm[i]-1) {
			c.set_xm_ghost(i,1);
		}
	}
    
    // if any entry is zero, set all local values to zero and set empty flag to true
	
	no_data = false;
    
	for (int i=0; i<ndim; i++) {
		if (c.get_nx_loc(i) == 0) {
			no_data = true;
			c.set_nx_loc(0,0);
			c.set_nx_loc(1,0);
			c.set_nx_loc(2,0);
            c.set_xm_loc(0,c.get_xm(0));
            c.set_xm_loc(1,c.get_xm(1));
            c.set_xm_loc(2,c.get_xm(2));
		}
	}
}

void block::set_grid(surface** surf, fields& f, cartesian& cart, fd_type& fd) {
    // set grid, metric, and jacobian in fields
    
    for (int i=0; i<3; i++) {
        dx[i] = 1./(double)(c.get_nx(i)-1);
    }
    
    double p, q, r;
    int nx = c.get_nx(0);
    int ny = c.get_nx(1);
    int nz = c.get_nx(2);
    int xm_loc = c.get_xm_loc(0)-c.get_xm(0);
    int ym_loc = c.get_xm_loc(1)-c.get_xm(1);
    int zm_loc = c.get_xm_loc(2)-c.get_xm(2);
    int nxm = c.get_xm_ghost(0);
    int nym = c.get_xm_ghost(1);
    int nzm = c.get_xm_ghost(2);
    int nxp = c.get_xp_ghost(0);
    int nyp = c.get_xp_ghost(1);
    int nzp = c.get_xp_ghost(2);
    int jj, kk, ll;
    
    // note: j,k,l loop over local values, while jj,kk,ll are the full-block equivalents (full surface is needed
    // for transfinite interpolation). nx, ny, nz are also full-block sizes
    // if only considering a 2D problem, omit some terms (z values are not used)
    
    int id;
    MPI_Comm_rank(MPI_COMM_WORLD, &id);
    
    for (int i=0; i<ndim; i++) {
        for (int j=mlb[0]-nxm; j<prb[0]+nxp; j++) {
            for (int k=mlb[1]-nym; k<prb[1]+nyp; k++) {
                for (int l=mlb[2]-nzm; l<prb[2]+nzp; l++) {
                    jj = xm_loc+j-nxm;
                    kk = ym_loc+k-nym;
                    ll = zm_loc+l-nzm;
                    p = (double)jj*dx[0];
                    q = (double)kk*dx[1];
                    r = (double)ll*dx[2];
                    f.x[i*nxd[0]+j*nxd[1]+k*nxd[2]+l] = ((1.-p)*surf[0]->get_x(i,kk,ll)+p*surf[1]->get_x(i,kk,ll)+
                                     (1.-q)*surf[2]->get_x(i,jj,ll)+q*surf[3]->get_x(i,jj,ll));
                    if (ndim == 3) {
                        f.x[i*nxd[0]+j*nxd[1]+k*nxd[2]+l] += (1.-r)*surf[4]->get_x(i,jj,kk)+r*surf[5]->get_x(i,jj,kk);
                    }
                    f.x[i*nxd[0]+j*nxd[1]+k*nxd[2]+l] -= ((1.-q)*(1.-p)*surf[0]->get_x(i,0,ll)+(1.-q)*p*surf[1]->get_x(i,0,ll)+
                                      q*(1.-p)*surf[0]->get_x(i,ny-1,ll)+q*p*surf[1]->get_x(i,ny-1,ll));
                    if (ndim == 3) {
                        f.x[i*nxd[0]+j*nxd[1]+k*nxd[2]+l] -= ((1.-p)*(1.-r)*surf[0]->get_x(i,kk,0)+p*(1.-r)*surf[1]->get_x(i,kk,0)+
                                          (1.-q)*(1.-r)*surf[2]->get_x(i,jj,0)+q*(1.-r)*surf[3]->get_x(i,jj,0)+
                                          (1.-p)*r*surf[0]->get_x(i,kk,nz-1)+p*r*surf[1]->get_x(i,kk,nz-1)+
                                          (1.-q)*r*surf[2]->get_x(i,jj,nz-1)+q*r*surf[3]->get_x(i,jj,nz-1));
                        f.x[i*nxd[0]+j*nxd[1]+k*nxd[2]+l] += ((1.-p)*(1.-q)*(1.-r)*surf[0]->get_x(i,0,0)+
                                          p*(1.-q)*(1.-r)*surf[1]->get_x(i,0,0)+
                                          (1.-p)*q*(1.-r)*surf[0]->get_x(i,ny-1,0)+
                                          (1.-p)*(1.-q)*r*surf[0]->get_x(i,0,nz-1)+
                                          p*q*(1.-r)*surf[1]->get_x(i,ny-1,0)+
                                          p*(1.-q)*r*surf[1]->get_x(i,0,nz-1)+
                                          (1.-p)*q*r*surf[0]->get_x(i,ny-1,nz-1)+
                                          p*q*r*surf[1]->get_x(i,ny-1,nz-1));
                    }
                }
            }
        }
    }
    
    // calculate metric derivatives
    // if 2d problem, set appropriate values for z derivatives to give correct 2d result
    
    double***** xp;
    
    xp = new double**** [3];
    
    for (int i=0; i<3; i++) {
        xp[i] = new double*** [3];
    }
    
    for (int i=0; i<3; i++) {
        for (int j=0; j<3; j++) {
            xp[i][j] = new double** [c.get_nx_loc(0)];
        }
    }
    
    for (int i=0; i<3; i++) {
        for (int j=0; j<3; j++) {
            for (int k=0; k<c.get_nx_loc(0); k++) {
                xp[i][j][k] = new double* [c.get_nx_loc(1)];
            }
        }
    }
    
    for (int i=0; i<3; i++) {
        for (int j=0; j<3; j++) {
            for (int k=0; k<c.get_nx_loc(0); k++) {
                for (int l=0; l<c.get_nx_loc(1); l++) {
                    xp[i][j][k][l] = new double [c.get_nx_loc(2)];
                }
            }
        }
    }
    
    for (int i=0; i<3; i++) {
        for (int j=0; j<3; j++) {
            for (int k=0; k<c.get_nx_loc(0); k++) {
                for (int l=0; l<c.get_nx_loc(1); l++) {
                    for (int m=0; m<c.get_nx_loc(2); m++) {
                        if (i == j) {
                            xp[i][j][k][l][m] = 1.;
                        } else {
                            xp[i][j][k][l][m] = 0.;
                        }
                    }
                }
            }
        }
    }
    
    // x derivatives
    
    for (int j=mlb[1]; j<prb[1]; j++) {
        for (int k=mlb[2]; k<prb[2]; k++) {
            for (int i=mlb[0]; i<mc[0]; i++) {
                // left boundaries
                for (int l=0; l<ndim; l++) {
                    xp[l][0][i-mlb[0]][j-mlb[1]][k-mlb[2]] = 0.;
                    for (int n=0; n<3*(fd.sbporder-1); n++) {
                        xp[l][0][i-mlb[0]][j-mlb[1]][k-mlb[2]] += fd.fdcoeff[i-mlb[0]+1][n]*f.x[l*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]/dx[0];
                    }
                }
            }
            for (int i=mc[0]; i<mrb[0]; i++) {
                // interior points
                for (int l=0; l<ndim; l++) {
                    xp[l][0][i-mlb[0]][j-mlb[1]][k-mlb[2]] = 0.;
                    for (int n=0; n<2*fd.sbporder-1; n++) {
                        xp[l][0][i-mlb[0]][j-mlb[1]][k-mlb[2]] += fd.fdcoeff[0][n]*f.x[l*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]/dx[0];
                    }
                }
            }
            for (int i=mrb[0]; i<prb[0]; i++) {
                // right boundaries
                for (int l=0; l<ndim; l++) {
                    xp[l][0][i-mlb[0]][j-mlb[1]][k-mlb[2]] = 0.;
                    for (int n=0; n<3*(fd.sbporder-1); n++) {
                        xp[l][0][i-mlb[0]][j-mlb[1]][k-mlb[2]] -= fd.fdcoeff[prb[0]-i][n]*f.x[l*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]/dx[0];
                    }
                }
            }
        }
    }
    
    // y derivatives
    
    for (int j=mlb[0]; j<prb[0]; j++) {
        for (int k=mlb[2]; k<prb[2]; k++) {
            for (int i=mlb[1]; i<mc[1]; i++) {
                // left boundaries
                for (int l=0; l<ndim; l++) {
                    xp[l][1][j-mlb[0]][i-mlb[1]][k-mlb[2]] = 0.;
                    for (int n=0; n<3*(fd.sbporder-1); n++) {
                        xp[l][1][j-mlb[0]][i-mlb[1]][k-mlb[2]] += fd.fdcoeff[i-mlb[1]+1][n]*f.x[l*nxd[0]+j*nxd[1]+(mlb[1]+n)*nxd[2]+k]/dx[1];
                    }
                }
            }
            for (int i=mc[1]; i<mrb[1]; i++) {
                // interior points
                for (int l=0; l<ndim; l++) {
                    xp[l][1][j-mlb[0]][i-mlb[1]][k-mlb[2]] = 0.;
                    for (int n=0; n<2*fd.sbporder-1; n++) {
                        xp[l][1][j-mlb[0]][i-mlb[1]][k-mlb[2]] += fd.fdcoeff[0][n]*f.x[l*nxd[0]+j*nxd[1]+(i-fd.sbporder+1+n)*nxd[2]+k]/dx[1];
                    }
                }
            }
            for (int i=mrb[1]; i<prb[1]; i++) {
                // right boundaries
                for (int l=0; l<ndim; l++) {
                    xp[l][1][j-mlb[0]][i-mlb[1]][k-mlb[2]] = 0.;
                    for (int n=0; n<3*(fd.sbporder-1); n++) {
                        xp[l][1][j-mlb[0]][i-mlb[1]][k-mlb[2]] -= fd.fdcoeff[prb[1]-i][n]*f.x[l*nxd[0]+j*nxd[1]+(prb[1]-1-n)*nxd[2]+k]/dx[1];
                    }
                }
            }
        }
    }
    
    // z derivatives
    
    for (int j=mlb[0]; j<prb[0]; j++) {
        for (int k=mlb[1]; k<prb[1]; k++) {
            for (int i=mlb[2]; i<mc[2]; i++) {
                // left boundaries
                for (int l=0; l<ndim; l++) {
                    xp[l][2][j-mlb[0]][k-mlb[1]][i-mlb[2]] = 0.;
                    for (int n=0; n<3*(fd.sbporder-1); n++) {
                        xp[l][2][j-mlb[0]][k-mlb[1]][i-mlb[2]] += fd.fdcoeff[i-mlb[2]+1][n]*f.x[l*nxd[0]+j*nxd[1]+k*nxd[2]+(mlb[2]+n)]/dx[2];
                    }
                }
            }
            for (int i=mc[2]; i<mrb[2]; i++) {
                // interior points
                for (int l=0; l<ndim; l++) {
                    xp[l][2][j-mlb[0]][k-mlb[1]][i-mlb[2]] = 0.;
                    for (int n=0; n<2*fd.sbporder-1; n++) {
                        xp[l][2][j-mlb[0]][k-mlb[1]][i-mlb[2]] += fd.fdcoeff[0][n]*f.x[l*nxd[0]+j*nxd[1]+k*nxd[2]+(i-fd.sbporder+1+n)]/dx[2];
                    }
                }
            }
            for (int i=mrb[2]; i<prb[2]; i++) {
                // right boundaries
                for (int l=0; l<ndim; l++) {
                    xp[l][2][j-mlb[0]][k-mlb[1]][i-mlb[2]] = 0.;
                    for (int n=0; n<3*(fd.sbporder-1); n++) {
                        xp[l][2][j-mlb[0]][k-mlb[1]][i-mlb[2]] -= fd.fdcoeff[prb[2]-i][n]*f.x[l*nxd[0]+j*nxd[1]+k*nxd[2]+(prb[2]-1-n)]/dx[2];
                    }
                }
            }
        }
    }
    
    // calculate metric derivatives and jacobian
                
    for (int i=mlb[0]; i<prb[0]; i++) {
        for (int j=mlb[1]; j<prb[1]; j++) {
            for (int k=mlb[2]; k<prb[2]; k++) {
                f.jac[i*nxd[1]+j*nxd[2]+k] = (xp[0][0][i-mlb[0]][j-mlb[1]][k-mlb[2]]*
                                              (xp[1][1][i-mlb[0]][j-mlb[1]][k-mlb[2]]*xp[2][2][i-mlb[0]][j-mlb[1]][k-mlb[2]]-
                                               xp[1][2][i-mlb[0]][j-mlb[1]][k-mlb[2]]*xp[2][1][i-mlb[0]][j-mlb[1]][k-mlb[2]])-
                                              xp[1][0][i-mlb[0]][j-mlb[1]][k-mlb[2]]*
                                              (xp[0][1][i-mlb[0]][j-mlb[1]][k-mlb[2]]*xp[2][2][i-mlb[0]][j-mlb[1]][k-mlb[2]]-
                                               xp[0][2][i-mlb[0]][j-mlb[1]][k-mlb[2]]*xp[2][1][i-mlb[0]][j-mlb[1]][k-mlb[2]])+
                                              xp[2][0][i-mlb[0]][j-mlb[1]][k-mlb[2]]*
                                              (xp[0][1][i-mlb[0]][j-mlb[1]][k-mlb[2]]*xp[1][2][i-mlb[0]][j-mlb[1]][k-mlb[2]]-
                                               xp[0][2][i-mlb[0]][j-mlb[1]][k-mlb[2]]*xp[1][1][i-mlb[0]][j-mlb[1]][k-mlb[2]]));
                for (int l=0; l<ndim; l++) {
                    for (int m=0; m<ndim; m++) {
                        f.metric[l*ndim*nxd[0]+m*nxd[0]+i*nxd[1]+j*nxd[2]+k] = ((xp[(m+1)%3][(l+1)%3][i-mlb[0]][j-mlb[1]][k-mlb[2]]*
                                                                                xp[(m+2)%3][(l+2)%3][i-mlb[0]][j-mlb[1]][k-mlb[2]]-
                                                                                xp[(m+1)%3][(l+2)%3][i-mlb[0]][j-mlb[1]][k-mlb[2]]*
                                                                                xp[(m+1)%3][(l+1)%3][i-mlb[0]][j-mlb[1]][k-mlb[2]])/
                                                                                f.jac[i*nxd[1]+j*nxd[2]+k]);
                    }
                }
            }
        }
    }
            
    for (int i=0; i<3; i++) {
        for (int j=0; j<3; j++) {
            for (int k=0; k<c.get_nx_loc(0); k++) {
                for (int l=0; l<c.get_nx_loc(1); l++) {
                    delete[] xp[i][j][k][l];
                }
            }
        }
    }
    
    for (int i=0; i<3; i++) {
        for (int j=0; j<3; j++) {
            for (int k=0; k<c.get_nx_loc(0); k++) {
                delete[] xp[i][j][k];
            }
        }
    }
    
    for (int i=0; i<3; i++) {
        for (int j=0; j<3; j++) {
            delete[] xp[i][j];
        }
    }
    
    for (int i=0; i<3; i++) {
        delete[] xp[i];
    }
    
    delete[] xp;
    
    // exhange data with neighbors
    
    f.exchange_grid();

}

void block::calc_df_mode2(const double dt, fields& f, fd_type& fd) {
    // calculates df of a low storage time step for a mode 2 problem
    
    // x derivatives
    
    double invrho = 1./mat.get_rho();
    
    for (int n=0; n<3*(fd.sbporder-1); n++) {
        for (int i=mlb[0]; i<mc[0]; i++) {
            for (int j=mlb[1]; j<prb[1]; j++) {
                f.df[0*nxd[0]+i*nxd[1]+j] += (dt*invrho/f.jac[i*nxd[1]+j]*fd.fdcoeff[i-mlb[0]+1][n]*
                                              (f.jac[(mlb[0]+n)*nxd[1]+j]*f.metric[0*ndim*nxd[0]+0*nxd[0]+(mlb[0]+n)*nxd[1]+j]*
                                               f.f[2*nxd[0]+(mlb[0]+n)*nxd[1]+j]+
                                               f.jac[(mlb[0]+n)*nxd[1]+j]*
                                               f.metric[0*ndim*nxd[0]+1*nxd[0]+(mlb[0]+n)*nxd[1]+j]*
                                               f.f[3*nxd[0]+(mlb[0]+n)*nxd[1]+j])/dx[0]);
                f.df[1*nxd[0]+i*nxd[1]+j] += (dt*invrho/f.jac[i*nxd[1]+j]*fd.fdcoeff[i-mlb[0]+1][n]*
                                              (f.jac[(mlb[0]+n)*nxd[1]+j]*f.metric[0*ndim*nxd[0]+0*nxd[0]+(mlb[0]+n)*nxd[1]+j]*
                                               f.f[3*nxd[0]+(mlb[0]+n)*nxd[1]+j]+
                                               f.jac[(mlb[0]+n)*nxd[1]+j]*f.metric[0*ndim*nxd[0]+1*nxd[0]+(mlb[0]+n)*nxd[1]+j]*
                                               f.f[4*nxd[0]+(mlb[0]+n)*nxd[1]+j])/dx[0]);
                f.df[2*nxd[0]+i*nxd[1]+j] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[i-mlb[0]+1][n]*f.f[0*nxd[0]+(mlb[0]+n)*nxd[1]+j]+
                                                 mat.get_lambda()*f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[i-mlb[0]+1][n]*f.f[1*nxd[0]+(mlb[0]+n)*nxd[1]+j])/dx[0];
                f.df[3*nxd[0]+i*nxd[1]+j] += dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[i-mlb[0]+1][n]*f.f[1*nxd[0]+(mlb[0]+n)*nxd[1]+j]+
                                                             f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[i-mlb[0]+1][n]*f.f[0*nxd[0]+(mlb[0]+n)*nxd[1]+j])/dx[0];
                f.df[4*nxd[0]+i*nxd[1]+j] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[i-mlb[0]+1][n]*f.f[1*nxd[0]+(mlb[0]+n)*nxd[1]+j]+
                                                 mat.get_lambda()*f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[i-mlb[0]+1][n]*f.f[0*nxd[0]+(mlb[0]+n)*nxd[1]+j])/dx[0];
            }
        }
    }

    for (int n=0; n<2*fd.sbporder-1; n++) {
        for (int i=mc[0]; i<mrb[0]; i++) {
            for (int j=mlb[1]; j<prb[1]; j++) {
                f.df[0*nxd[0]+i*nxd[1]+j] += (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[0][n]*(f.jac[(i-fd.sbporder+1+n)*nxd[1]+j]*
                                                                         f.metric[0*ndim*nxd[0]+0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j]*
                                                                         f.f[2*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j]+
                                                                         f.jac[(i-fd.sbporder+1+n)*nxd[1]+j]*
                                                                         f.metric[0*ndim*nxd[0]+1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j]*
                                                                         f.f[3*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j])/dx[0]);
                f.df[1*nxd[0]+i*nxd[1]+j] += (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[0][n]*(f.jac[(i-fd.sbporder+1+n)*nxd[1]+j]*
                                                                         f.metric[0*ndim*nxd[0]+0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j]*
                                                                         f.f[3*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j]+
                                                                         f.jac[(i-fd.sbporder+1+n)*nxd[1]+j]*
                                                                         f.metric[0*ndim*nxd[0]+1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j]*
                                                                         f.f[4*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j])/dx[0]);
                f.df[2*nxd[0]+i*nxd[1]+j] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[0][n]*f.f[0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j]+
                                                 mat.get_lambda()*f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[0][n]*f.f[1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j])/dx[0];
                f.df[3*nxd[0]+i*nxd[1]+j] += dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[0][n]*f.f[1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j]+
                                                             f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[0][n]*f.f[0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j])/dx[0];
                f.df[4*nxd[0]+i*nxd[1]+j] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[0][n]*f.f[1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j]+
                                                 mat.get_lambda()*f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[0][n]*f.f[0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j])/dx[0];
            }
        }
    }

    for (int n=0; n<3*(fd.sbporder-1); n++) {
        for (int i=mrb[0]; i<prb[0]; i++) {
            for (int j=mlb[1]; j<prb[1]; j++) {
                f.df[0*nxd[0]+i*nxd[1]+j] -= (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[prb[0]-i][n]*(f.jac[(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.metric[0*ndim*nxd[0]+0*nxd[0]+(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.f[2*nxd[0]+(prb[0]-1-n)*nxd[1]+j]+
                                                                       f.jac[(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.metric[0*ndim*nxd[0]+1*nxd[0]+(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.f[3*nxd[0]+(prb[0]-1-n)*nxd[1]+j])/dx[0]);
                f.df[1*nxd[0]+i*nxd[1]+j] -= (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[prb[0]-i][n]*(f.jac[(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.metric[0*ndim*nxd[0]+0*nxd[0]+(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.f[3*nxd[0]+(prb[0]-1-n)*nxd[1]+j]+
                                                                       f.jac[(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.metric[0*ndim*nxd[0]+1*nxd[0]+(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.f[4*nxd[0]+(prb[0]-1-n)*nxd[1]+j])/dx[0]);
                f.df[2*nxd[0]+i*nxd[1]+j] -= dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[prb[0]-i][n]*f.f[0*nxd[0]+(prb[0]-1-n)*nxd[1]+j]+
                                                 mat.get_lambda()*f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[prb[0]-i][n]*f.f[1*nxd[0]+(prb[0]-1-n)*nxd[1]+j])/dx[0];
                f.df[3*nxd[0]+i*nxd[1]+j] -= dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[prb[0]-i][n]*f.f[1*nxd[0]+(prb[0]-1-n)*nxd[1]+j]+
                                                             f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[prb[0]-i][n]*f.f[0*nxd[0]+(prb[0]-1-n)*nxd[1]+j])/dx[0];
                f.df[4*nxd[0]+i*nxd[1]+j] -= dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[prb[0]-i][n]*f.f[1*nxd[0]+(prb[0]-1-n)*nxd[1]+j]+
                                                 mat.get_lambda()*f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[prb[0]-i][n]*f.f[0*nxd[0]+(prb[0]-1-n)*nxd[1]+j])/dx[0];
            }
        }
    }

    // y derivatives
    
    for (int i=mlb[0]; i<prb[0]; i++) {
        for (int j=mlb[1]; j<mc[1]; j++) {
            for (int n=0; n<3*(fd.sbporder-1); n++) {
                f.df[0*nxd[0]+i*nxd[1]+j] += (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[j-mlb[1]+1][n]*(f.jac[i*nxd[1]+(mlb[1]+n)]*
                                                                         f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(mlb[1]+n)]*
                                                                         f.f[2*nxd[0]+i*nxd[1]+(mlb[1]+n)]+
                                                                         f.jac[i*nxd[1]+(mlb[1]+n)]*
                                                                         f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(mlb[1]+n)]*
                                                                         f.f[3*nxd[0]+i*nxd[1]+(mlb[1]+n)])/dx[1]);
                f.df[1*nxd[0]+i*nxd[1]+j] += (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[j-mlb[1]+1][n]*(f.jac[i*nxd[1]+(mlb[1]+n)]*
                                                                         f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(mlb[1]+n)]*
                                                                         f.f[3*nxd[0]+i*nxd[1]+(mlb[1]+n)]+
                                                                         f.jac[i*nxd[1]+(mlb[1]+n)]*
                                                                         f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(mlb[1]+n)]*
                                                                         f.f[4*nxd[0]+i*nxd[1]+(mlb[1]+n)])/dx[1]);
                f.df[2*nxd[0]+i*nxd[1]+j] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[j-mlb[1]+1][n]*f.f[0*nxd[0]+i*nxd[1]+(mlb[1]+n)]+
                                                 mat.get_lambda()*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[j-mlb[1]+1][n]*f.f[1*nxd[0]+i*nxd[1]+(mlb[1]+n)])/dx[1];
                f.df[3*nxd[0]+i*nxd[1]+j] += dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[j-mlb[1]+1][n]*f.f[1*nxd[0]+i*nxd[1]+(mlb[1]+n)]+
                                                             f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[j-mlb[1]+1][n]*f.f[0*nxd[0]+i*nxd[1]+(mlb[1]+n)])/dx[1];
                f.df[4*nxd[0]+i*nxd[1]+j] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[j-mlb[1]+1][n]*f.f[1*nxd[0]+i*nxd[1]+(mlb[1]+n)]+
                                                 mat.get_lambda()*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[j-mlb[1]+1][n]*f.f[0*nxd[0]+i*nxd[1]+(mlb[1]+n)])/dx[1];
            }
        }
        
        for (int j=mc[1]; j<mrb[1]; j++) {
            for (int n=0; n<2*fd.sbporder-1; n++) {
                f.df[0*nxd[0]+i*nxd[1]+j] += (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[0][n]*(f.jac[i*nxd[1]+(j-fd.sbporder+1+n)]*
                                                                f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)]*
                                                                f.f[2*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)]+
                                                                f.jac[i*nxd[1]+(j-fd.sbporder+1+n)]*
                                                                f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)]*
                                                                f.f[3*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)])/dx[1]);
                f.df[1*nxd[0]+i*nxd[1]+j] += (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[0][n]*(f.jac[i*nxd[1]+(j-fd.sbporder+1+n)]*
                                                                f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)]*
                                                                f.f[3*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)]+
                                                                f.jac[i*nxd[1]+(j-fd.sbporder+1+n)]*
                                                                f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)]*
                                                                f.f[4*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)])/dx[1]);
                f.df[2*nxd[0]+i*nxd[1]+j] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)]+
                                                 mat.get_lambda()*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[0][n]*f.f[1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)])/dx[1];
                f.df[3*nxd[0]+i*nxd[1]+j] += dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[0][n]*f.f[1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)]+
                                                             f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)])/dx[1];
                f.df[4*nxd[0]+i*nxd[1]+j] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[0][n]*f.f[1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)]+
                                                 mat.get_lambda()*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)])/dx[1];
            }
        }
        
        for (int j=mrb[1]; j<prb[1]; j++) {
            for (int n=0; n<3*(fd.sbporder-1); n++) {
                f.df[0*nxd[0]+i*nxd[1]+j] -= (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[prb[1]-j][n]*(f.jac[i*nxd[1]+(prb[1]-1-n)]*
                                                                       f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(prb[1]-1-n)]*
                                                                       f.f[2*nxd[0]+i*nxd[1]+(prb[1]-1-n)]+
                                                                       f.jac[(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(prb[1]-1-n)]*
                                                                       f.f[3*nxd[0]+i*nxd[1]+(prb[1]-1-n)])/dx[1]);
                f.df[1*nxd[0]+i*nxd[1]+j] -= (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[prb[1]-j][n]*(f.jac[i*nxd[1]+(prb[1]-1-n)]*
                                                                       f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(prb[1]-1-n)]*
                                                                       f.f[3*nxd[0]+i*nxd[1]+(prb[1]-1-n)]+
                                                                       f.jac[i*nxd[1]+(prb[1]-1-n)]*
                                                                       f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(prb[1]-1-n)]*
                                                                       f.f[4*nxd[0]+i*nxd[1]+(prb[1]-1-n)])/dx[1]);
                f.df[2*nxd[0]+i*nxd[1]+j] -= dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[prb[1]-j][n]*f.f[0*nxd[0]+i*nxd[1]+(prb[1]-1-n)]+
                                                 mat.get_lambda()*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[prb[1]-j][n]*f.f[1*nxd[0]+i*nxd[1]+(prb[1]-1-n)])/dx[1];
                f.df[3*nxd[0]+i*nxd[1]+j] -= dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[prb[1]-j][n]*f.f[1*nxd[0]+i*nxd[1]+(prb[1]-1-n)]+
                                                             f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[prb[1]-j][n]*f.f[0*nxd[0]+i*nxd[1]+(prb[1]-1-n)])/dx[1];
                f.df[4*nxd[0]+i*nxd[1]+j] -= dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[prb[1]-j][n]*f.f[1*nxd[0]+i*nxd[1]+(prb[1]-1-n)]+
                                                 mat.get_lambda()*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                 fd.fdcoeff[prb[1]-j][n]*f.f[0*nxd[0]+i*nxd[1]+(prb[1]-1-n)])/dx[1];
            }
        }
    }
    
}

void block::calc_df_mode3(const double dt, fields& f, fd_type& fd) {
    // calculates df of a low storage time step for a mode 3 problem
    
    // x derivatives
    
    double invrho = 1./mat.get_rho();
    
    for (int n=0; n<3*(fd.sbporder-1); n++) {
        for (int i=mlb[0]; i<mc[0]; i++) {
            for (int j=mlb[1]; j<prb[1]; j++) {
                f.df[0*nxd[0]+i*nxd[1]+j] += (dt*invrho/f.jac[i*nxd[1]+j]*fd.fdcoeff[i-mlb[0]+1][n]*
                                              (f.jac[(mlb[0]+n)*nxd[1]+j]*f.metric[0*ndim*nxd[0]+0*nxd[0]+(mlb[0]+n)*nxd[1]+j]*
                                               f.f[1*nxd[0]+(mlb[0]+n)*nxd[1]+j]+
                                               f.jac[(mlb[0]+n)*nxd[1]+j]*
                                               f.metric[0*ndim*nxd[0]+1*nxd[0]+(mlb[0]+n)*nxd[1]+j]*
                                               f.f[2*nxd[0]+(mlb[0]+n)*nxd[1]+j])/dx[0]);
                f.df[1*nxd[0]+i*nxd[1]+j] += dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[i-mlb[0]+1][n]*f.f[0*nxd[0]+(mlb[0]+n)*nxd[1]+j])/dx[0];
                f.df[2*nxd[0]+i*nxd[1]+j] += dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[i-mlb[0]+1][n]*f.f[0*nxd[0]+(mlb[0]+n)*nxd[1]+j])/dx[0];
            }
        }
    }
    
    for (int n=0; n<2*fd.sbporder-1; n++) {
        for (int i=mc[0]; i<mrb[0]; i++) {
            for (int j=mlb[1]; j<prb[1]; j++) {
                f.df[0*nxd[0]+i*nxd[1]+j] += (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[0][n]*(f.jac[(i-fd.sbporder+1+n)*nxd[1]+j]*
                                                                f.metric[0*ndim*nxd[0]+0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j]*
                                                                f.f[1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j]+
                                                                f.jac[(i-fd.sbporder+1+n)*nxd[1]+j]*
                                                                f.metric[0*ndim*nxd[0]+1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j]*
                                                                f.f[2*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j])/dx[0]);
                f.df[1*nxd[0]+i*nxd[1]+j] += dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[0][n]*f.f[0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j])/dx[0];
                f.df[2*nxd[0]+i*nxd[1]+j] += dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[0][n]*f.f[0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j])/dx[0];
            }
        }
    }
    
    for (int n=0; n<3*(fd.sbporder-1); n++) {
        for (int i=mrb[0]; i<prb[0]; i++) {
            for (int j=mlb[1]; j<prb[1]; j++) {
                f.df[0*nxd[0]+i*nxd[1]+j] -= (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[prb[0]-i][n]*(f.jac[(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.metric[0*ndim*nxd[0]+0*nxd[0]+(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.f[1*nxd[0]+(prb[0]-1-n)*nxd[1]+j]+
                                                                       f.jac[(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.metric[0*ndim*nxd[0]+1*nxd[0]+(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.f[2*nxd[0]+(prb[0]-1-n)*nxd[1]+j])/dx[0]);
                f.df[1*nxd[0]+i*nxd[1]+j] -= dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[prb[0]-i][n]*f.f[0*nxd[0]+(prb[0]-1-n)*nxd[1]+j])/dx[0];
                f.df[2*nxd[0]+i*nxd[1]+j] -= dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[prb[0]-i][n]*f.f[0*nxd[0]+(prb[0]-1-n)*nxd[1]+j])/dx[0];
            }
        }
    }
    
    // y derivatives
    
    for (int i=mlb[0]; i<prb[0]; i++) {
        for (int j=mlb[1]; j<mc[1]; j++) {
            for (int n=0; n<3*(fd.sbporder-1); n++) {
                f.df[0*nxd[0]+i*nxd[1]+j] += (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[j-mlb[1]+1][n]*(f.jac[i*nxd[1]+(mlb[1]+n)]*
                                                                         f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(mlb[1]+n)]*
                                                                         f.f[1*nxd[0]+i*nxd[1]+(mlb[1]+n)]+
                                                                         f.jac[i*nxd[1]+(mlb[1]+n)]*
                                                                         f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(mlb[1]+n)]*
                                                                         f.f[2*nxd[0]+i*nxd[1]+(mlb[1]+n)])/dx[1]);
                f.df[1*nxd[0]+i*nxd[1]+j] += dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[j-mlb[1]+1][n]*f.f[0*nxd[0]+i*nxd[1]+(mlb[1]+n)])/dx[1];
                f.df[2*nxd[0]+i*nxd[1]+j] += dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[j-mlb[1]+1][n]*f.f[0*nxd[0]+i*nxd[1]+(mlb[1]+n)])/dx[1];
            }
        }
        
        for (int j=mc[1]; j<mrb[1]; j++) {
            for (int n=0; n<2*fd.sbporder-1; n++) {
                f.df[0*nxd[0]+i*nxd[1]+j] += (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[0][n]*(f.jac[i*nxd[1]+(j-fd.sbporder+1+n)]*
                                                                f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)]*
                                                                f.f[1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)]+
                                                                f.jac[i*nxd[1]+(j-fd.sbporder+1+n)]*
                                                                f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)]*
                                                                f.f[2*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)])/dx[1]);
                f.df[1*nxd[0]+i*nxd[1]+j] += dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)])/dx[1];
                f.df[2*nxd[0]+i*nxd[1]+j] += dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)])/dx[1];
            }
        }
        
        for (int j=mrb[1]; j<prb[1]; j++) {
            for (int n=0; n<3*(fd.sbporder-1); n++) {
                f.df[0*nxd[0]+i*nxd[1]+j] -= (dt/mat.get_rho()/f.jac[i*nxd[1]+j]*
                                              fd.fdcoeff[prb[1]-j][n]*(f.jac[i*nxd[1]+(prb[1]-1-n)]*
                                                                       f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(prb[1]-1-n)]*
                                                                       f.f[1*nxd[0]+i*nxd[1]+(prb[1]-1-n)]+
                                                                       f.jac[(prb[0]-1-n)*nxd[1]+j]*
                                                                       f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(prb[1]-1-n)]*
                                                                       f.f[2*nxd[0]+i*nxd[1]+(prb[1]-1-n)])/dx[1]);
                f.df[1*nxd[0]+i*nxd[1]+j] -= dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[prb[1]-j][n]*f.f[0*nxd[0]+i*nxd[1]+(prb[1]-1-n)])/dx[1];
                f.df[2*nxd[0]+i*nxd[1]+j] -= dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j]*
                                                             fd.fdcoeff[prb[1]-j][n]*f.f[0*nxd[0]+i*nxd[1]+(prb[1]-1-n)])/dx[1];
            }
        }
    }
    
}

void block::calc_df_3d(const double dt, fields& f, fd_type& fd) {
    // calculates df of a low storage time step for a 3d problem
    
    // x derivatives
    
    double invrho = 1./mat.get_rho();
    
    for (int n=0; n<3*(fd.sbporder-1); n++) {
        for (int i=mlb[0]; i<mc[0]; i++) {
            for (int j=mlb[1]; j<prb[1]; j++) {
                for (int k=mlb[2]; k<prb[2]; k++) {
                    f.df[0*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[i-mlb[0]+1][n]*
                                                           (f.jac[(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+0*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[3*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+1*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[4*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+2*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[5*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k])/dx[0]);
                    f.df[1*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[i-mlb[0]+1][n]*
                                                           (f.jac[(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+0*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[4*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+1*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[6*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+2*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[7*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k])/dx[0]);
                    f.df[2*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[i-mlb[0]+1][n]*
                                                           (f.jac[(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+0*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[5*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+1*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[7*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+2*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[8*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k])/dx[0]);
                    f.df[3*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[i-mlb[0]+1][n]*f.f[0*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[i-mlb[0]+1][n]*f.f[1*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                                                f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[i-mlb[0]+1][n]*f.f[2*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]))/dx[0];
                    f.df[4*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[i-mlb[0]+1][n]*f.f[1*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                                          f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[i-mlb[0]+1][n]*f.f[0*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k])/dx[0];
                    f.df[5*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[i-mlb[0]+1][n]*f.f[2*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                                          f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[i-mlb[0]+1][n]*f.f[0*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k])/dx[0];
                    f.df[6*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[i-mlb[0]+1][n]*f.f[1*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[i-mlb[0]+1][n]*f.f[0*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                                                f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[i-mlb[0]+1][n]*f.f[2*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]))/dx[0];
                    f.df[7*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[i-mlb[0]+1][n]*f.f[2*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                                          f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[i-mlb[0]+1][n]*f.f[2*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k])/dx[0];
                    f.df[8*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[i-mlb[0]+1][n]*f.f[2*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[i-mlb[0]+1][n]*f.f[0*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]+
                                                                                f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[i-mlb[0]+1][n]*f.f[1*nxd[0]+(mlb[0]+n)*nxd[1]+j*nxd[2]+k]))/dx[0];
                }
            }
        }
    }

    for (int n=0; n<2*fd.sbporder-1; n++) {
        for (int i=mc[0]; i<mrb[0]; i++) {
            for (int j=mlb[1]; j<prb[1]; j++) {
                for (int k=mlb[2]; k<prb[2]; k++) {
                    f.df[0*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[0][n]*
                                                           (f.jac[(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[3*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[4*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+2*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[5*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k])/dx[0]);
                    f.df[1*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[0][n]*
                                                           (f.jac[(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[4*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[6*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+2*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[7*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k])/dx[0]);
                    f.df[2*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[0][n]*
                                                           (f.jac[(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[5*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[7*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+2*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[8*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k])/dx[0]);
                    f.df[3*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[0][n]*f.f[0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                                                f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[2*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]))/dx[0];
                    f.df[4*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                                          f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k])/dx[0];
                    f.df[5*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[2*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                                          f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k])/dx[0];
                    f.df[6*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[0][n]*f.f[1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                                                f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[2*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]))/dx[0];
                    f.df[7*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[2*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                                          f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[2*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k])/dx[0];
                    f.df[8*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[0][n]*f.f[2*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[0*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]+
                                                                                f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[1*nxd[0]+(i-fd.sbporder+1+n)*nxd[1]+j*nxd[2]+k]))/dx[0];
                }
            }
        }
    }

    for (int n=0; n<3*(fd.sbporder-1); n++) {
        for (int i=mrb[0]; i<prb[0]; i++) {
            for (int j=mlb[1]; j<prb[1]; j++) {
                for (int k=mlb[2]; k<prb[2]; k++) {
                    f.df[0*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[prb[0]-i][n]*
                                                           (f.jac[(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+0*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[3*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+1*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[4*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+2*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[5*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k])/dx[0]);
                    f.df[1*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[prb[0]-i][n]*
                                                           (f.jac[(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+0*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[4*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+1*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[6*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+2*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[7*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k])/dx[0]);
                    f.df[2*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[prb[0]-i][n]*
                                                           (f.jac[(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+0*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[5*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+1*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[7*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                            f.jac[(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*f.metric[0*ndim*nxd[0]+2*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]*
                                                            f.f[8*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k])/dx[0]);
                    f.df[3*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[prb[0]-i][n]*f.f[0*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[0]-i][n]*f.f[1*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                                                f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[0]-i][n]*f.f[2*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]))/dx[0];
                    f.df[4*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[0]-i][n]*f.f[1*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                                          f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[0]-i][n]*f.f[0*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k])/dx[0];
                    f.df[5*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[0]-i][n]*f.f[2*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                                          f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[0]-i][n]*f.f[0*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k])/dx[0];
                    f.df[6*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[prb[0]-i][n]*f.f[1*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[0]-i][n]*f.f[0*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                                                f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[0]-i][n]*f.f[2*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]))/dx[0];
                    f.df[7*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*mat.get_g()*(f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[0]-i][n]*f.f[2*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                                          f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[0]-i][n]*f.f[2*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k])/dx[0];
                    f.df[8*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[0*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[prb[0]-i][n]*f.f[2*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[0*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[0]-i][n]*f.f[0*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]+
                                                                                f.metric[0*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[0]-i][n]*f.f[1*nxd[0]+(prb[0]-1-n)*nxd[1]+j*nxd[2]+k]))/dx[0];
                }
            }
        }
    }
    
    // y derivatives
    
    for (int n=0; n<3*(fd.sbporder-1); n++) {
        for (int i=mlb[0]; i<prb[0]; i++) {
            for (int j=mlb[1]; j<mc[1]; j++) {
                for (int k=mlb[2]; k<prb[2]; k++) {
                    f.df[0*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[j-mlb[1]+1][n]*
                                                           (f.jac[i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*
                                                            f.f[3*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*
                                                            f.f[4*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*
                                                            f.f[5*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k])/dx[1]);
                    f.df[1*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[j-mlb[1]+1][n]*
                                                           (f.jac[i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*
                                                            f.f[4*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*
                                                            f.f[6*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*
                                                            f.f[7*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k])/dx[1]);
                    f.df[2*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[j-mlb[1]+1][n]*
                                                           (f.jac[i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*
                                                            f.f[5*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*
                                                            f.f[7*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]*
                                                            f.f[8*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k])/dx[1]);
                    f.df[3*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[j-mlb[1]+1][n]*f.f[0*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[j-mlb[1]+1][n]*f.f[1*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                                                f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[j-mlb[1]+1][n]*f.f[2*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]))/dx[1];
                    f.df[4*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[j-mlb[1]+1][n]*f.f[1*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                                          f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[j-mlb[1]+1][n]*f.f[0*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k])/dx[1];
                    f.df[5*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+i*nxd[2]+k]*
                                                                          fd.fdcoeff[j-mlb[1]+1][n]*f.f[2*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                                          f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[j-mlb[1]+1][n]*f.f[0*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k])/dx[1];
                    f.df[6*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[j-mlb[1]+1][n]*f.f[1*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[j-mlb[1]+1][n]*f.f[0*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                                                f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[j-mlb[1]+1][n]*f.f[2*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]))/dx[1];
                    f.df[7*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[j-mlb[1]+1][n]*f.f[2*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                                          f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[j-mlb[1]+1][n]*f.f[2*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k])/dx[1];
                    f.df[8*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[j-mlb[1]+1][n]*f.f[2*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[j-mlb[1]+1][n]*f.f[0*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]+
                                                                                f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[j-mlb[1]+1][n]*f.f[1*nxd[0]+i*nxd[1]+(mlb[1]+n)*nxd[2]+k]))/dx[1];
                }
            }
        }
    }
    
    for (int n=0; n<2*fd.sbporder-1; n++) {
        for (int i=mlb[0]; i<prb[0]; i++) {
            for (int j=mc[1]; j<mrb[1]; j++) {
                for (int k=mlb[2]; k<prb[2]; k++) {
                    f.df[0*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[0][n]*
                                                           (f.jac[i*nxd[1]+j*nxd[2]+k]*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*
                                                            f.f[3*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*
                                                            f.f[4*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*
                                                            f.f[5*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k])/dx[1]);
                    f.df[1*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[0][n]*
                                                           (f.jac[i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*
                                                            f.f[4*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*
                                                            f.f[6*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*
                                                            f.f[7*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k])/dx[1]);
                    f.df[2*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[0][n]*
                                                           (f.jac[i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*
                                                            f.f[5*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*
                                                            f.f[7*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]*
                                                            f.f[8*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k])/dx[1]);
                    f.df[3*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                                                f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[2*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]))/dx[1];
                    f.df[4*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                                          f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k])/dx[1];
                    f.df[5*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[2*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                                          f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k])/dx[1];
                    f.df[6*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[0][n]*f.f[1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                                                f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[2*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]))/dx[1];
                    f.df[7*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[2*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                                          f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[2*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k])/dx[1];
                    f.df[8*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[0][n]*f.f[2*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]+
                                                                                f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[1*nxd[0]+i*nxd[1]+(j-fd.sbporder+1+n)*nxd[2]+k]))/dx[1];
                }
            }
        }
    }
    
    for (int n=0; n<3*(fd.sbporder-1); n++) {
        for (int i=mlb[0]; i<prb[0]; i++) {
            for (int j=mrb[1]; j<prb[1]; j++) {
                for (int k=mlb[2]; k<prb[2]; k++) {
                    f.df[0*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[prb[1]-j][n]*
                                                           (f.jac[i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*
                                                            f.f[3*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*
                                                            f.f[4*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*
                                                            f.f[5*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k])/dx[1]);
                    f.df[1*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[prb[1]-j][n]*
                                                           (f.jac[i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*
                                                            f.f[4*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*
                                                            f.f[6*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*
                                                            f.f[7*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k])/dx[1]);
                    f.df[2*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[prb[1]-j][n]*
                                                           (f.jac[i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*
                                                            f.f[5*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*
                                                            f.f[7*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                            f.jac[i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]*
                                                            f.f[8*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k])/dx[1]);
                    f.df[3*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[prb[1]-j][n]*f.f[0*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[1]-j][n]*f.f[1*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                                                f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[1]-j][n]*f.f[2*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]))/dx[1];
                    f.df[4*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[1]-j][n]*f.f[1*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                                          f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[1]-j][n]*f.f[0*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k])/dx[1];
                    f.df[5*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[1]-j][n]*f.f[2*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                                          f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[1]-j][n]*f.f[0*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k])/dx[1];
                    f.df[6*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[prb[1]-j][n]*f.f[1*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[1]-j][n]*f.f[0*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                                                f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[1]-j][n]*f.f[2*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]))/dx[1];
                    f.df[7*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*mat.get_g()*(f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[1]-j][n]*f.f[2*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                                          f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[1]-j][n]*f.f[2*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k])/dx[1];
                    f.df[8*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[1*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[prb[1]-j][n]*f.f[2*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                              mat.get_lambda()*(f.metric[1*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[1]-j][n]*f.f[0*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]+
                                                                                f.metric[1*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[1]-j][n]*f.f[1*nxd[0]+i*nxd[1]+(prb[1]-1-n)*nxd[2]+k]))/dx[1];
                }
            }
        }
    }
    
    // z derivatives
    
    for (int n=0; n<3*(fd.sbporder-1); n++) {
        for (int i=mlb[0]; i<prb[0]; i++) {
            for (int j=mlb[1]; j<prb[1]; j++) {
                for (int k=mlb[2]; k<mc[2]; k++) {
                    f.df[0*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[k-mlb[2]+1][n]*
                                                           (f.jac[i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*
                                                            f.f[3*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*
                                                            f.f[4*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*
                                                            f.f[5*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)])/dx[2]);
                    f.df[1*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[k-mlb[2]+1][n]*
                                                           (f.jac[i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*
                                                            f.f[4*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*
                                                            f.f[6*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*
                                                            f.f[7*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)])/dx[2]);
                    f.df[2*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[k-mlb[2]+1][n]*
                                                           (f.jac[i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*
                                                            f.f[5*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*
                                                            f.f[7*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]*
                                                            f.f[8*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)])/dx[2]);
                    f.df[3*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[k-mlb[2]+1][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                              mat.get_lambda()*(f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[k-mlb[2]+1][n]*f.f[1*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                                                f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[k-mlb[2]+1][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]))/dx[2];
                    f.df[4*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[k-mlb[2]+1][n]*f.f[1*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                                          f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[k-mlb[2]+1][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)])/dx[2];
                    f.df[5*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[k-mlb[2]+1][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                                          f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[k-mlb[2]+1][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)])/dx[2];
                    f.df[6*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[k-mlb[2]+1][n]*f.f[1*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                              mat.get_lambda()*(f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[k-mlb[2]+1][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                                                f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[k-mlb[2]+1][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]))/dx[2];
                    f.df[7*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[k-mlb[2]+1][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                                          f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[k-mlb[2]+1][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)])/dx[2];
                    f.df[8*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[k-mlb[2]+1][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                              mat.get_lambda()*(f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[k-mlb[2]+1][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]+
                                                                                f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[k-mlb[2]+1][n]*f.f[1*nxd[0]+i*nxd[1]+j*nxd[2]+(mlb[2]+n)]))/dx[2];
                }
            }
        }
    }
    
    for (int n=0; n<2*fd.sbporder-1; n++) {
        for (int i=mlb[0]; i<prb[0]; i++) {
            for (int j=mlb[1]; j<prb[1]; j++) {
                for (int k=mc[2]; k<mrb[2]; k++) {
                    f.df[0*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[0][n]*
                                                           (f.jac[i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*
                                                            f.f[3*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*
                                                            f.f[4*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*
                                                            f.f[5*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)])/dx[2]);
                    f.df[1*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[0][n]*
                                                           (f.jac[i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*
                                                            f.f[4*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*
                                                            f.f[6*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*
                                                            f.f[7*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)])/dx[2]);
                    f.df[2*nxd[0]+i*nxd[1]+j*nxd[2]+k] += (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[0][n]*
                                                           (f.jac[i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*
                                                            f.f[5*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*
                                                            f.f[7*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]*
                                                            f.f[8*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)])/dx[2]);
                    f.df[3*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                              mat.get_lambda()*(f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[1*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                                                f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]))/dx[2];
                    f.df[4*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[1*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                                          f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)])/dx[2];
                    f.df[5*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                                          f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)])/dx[2];
                    f.df[6*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[0][n]*f.f[1*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                              mat.get_lambda()*(f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                                                f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]))/dx[2];
                    f.df[7*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*mat.get_g()*(f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                                          f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[0][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)])/dx[2];
                    f.df[8*nxd[0]+i*nxd[1]+j*nxd[2]+k] += dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[0][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                              mat.get_lambda()*(f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]+
                                                                                f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[0][n]*f.f[1*nxd[0]+i*nxd[1]+j*nxd[2]+(k-fd.sbporder+1+n)]))/dx[2];
                }
            }
        }
    }
    
    for (int n=0; n<3*(fd.sbporder-1); n++) {
        for (int i=mlb[0]; i<prb[0]; i++) {
            for (int j=mlb[1]; j<prb[1]; j++) {
                for (int k=mrb[2]; k<prb[2]; k++) {
                    f.df[0*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[prb[2]-k][n]*
                                                           (f.jac[i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*
                                                            f.f[3*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*
                                                            f.f[4*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*
                                                            f.f[5*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)])/dx[2]);
                    f.df[1*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[prb[2]-k][n]*
                                                           (f.jac[i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*
                                                            f.f[4*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*
                                                            f.f[6*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*
                                                            f.f[7*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)])/dx[2]);
                    f.df[2*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= (dt*invrho/f.jac[i*nxd[1]+j*nxd[2]+k]*fd.fdcoeff[prb[2]-k][n]*
                                                           (f.jac[i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*
                                                            f.f[5*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*
                                                            f.f[7*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                            f.jac[i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]*
                                                            f.f[8*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)])/dx[2]);
                    f.df[3*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[prb[2]-k][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                              mat.get_lambda()*(f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[2]-k][n]*f.f[1*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                                                f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[2]-k][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]))/dx[2];
                    f.df[4*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*mat.get_g()*(f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[2]-k][n]*f.f[1*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                                          f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[2]-k][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)])/dx[2];
                    f.df[5*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*mat.get_g()*(f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[2]-k][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                                          f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[2]-k][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)])/dx[2];
                    f.df[6*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[prb[2]-k][n]*f.f[1*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                              mat.get_lambda()*(f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[2]-k][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                                                f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[2]-k][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]))/dx[2];
                    f.df[7*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*mat.get_g()*(f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[2]-k][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                                          f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                          fd.fdcoeff[prb[2]-k][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)])/dx[2];
                    f.df[8*nxd[0]+i*nxd[1]+j*nxd[2]+k] -= dt*((mat.get_g()*2.+mat.get_lambda())*f.metric[2*ndim*nxd[0]+2*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                              fd.fdcoeff[prb[2]-k][n]*f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                              mat.get_lambda()*(f.metric[2*ndim*nxd[0]+0*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[2]-k][n]*f.f[0*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]+
                                                                                f.metric[2*ndim*nxd[0]+1*nxd[0]+i*nxd[1]+j*nxd[2]+k]*
                                                                                fd.fdcoeff[prb[2]-k][n]*f.f[1*nxd[0]+i*nxd[1]+j*nxd[2]+(prb[2]-1-n)]))/dx[2];
                }
            }
        }
    }
    
}

void block::init_fields(fields& f) {
    for (int i=mlb[0]; i<prb[0]; i++) {
        for (int j=mlb[1]; j<prb[1]; j++) {
            for (int k=mlb[2]; k<prb[2]; k++) {
                f.f[2*nxd[0]+i*nxd[1]+j*nxd[2]+k] = -exp(-pow(f.x[0*nxd[0]+i*nxd[1]+j*nxd[2]+k]-0.5,2)/0.005
                                                         -pow(f.x[1*nxd[0]+i*nxd[1]+j*nxd[2]+k]-0.5,2)/0.005
                                                         -pow(f.x[2*nxd[0]+i*nxd[1]+j*nxd[2]+k]-0.5,2)/0.005);
                f.f[8*nxd[0]+i*nxd[1]+j*nxd[2]+k] = -exp(-pow(f.x[0*nxd[0]+i*nxd[1]+j*nxd[2]+k]-0.5,2)/0.005
                                                         -pow(f.x[1*nxd[0]+i*nxd[1]+j*nxd[2]+k]-0.5,2)/0.005
                                                         -pow(f.x[2*nxd[0]+i*nxd[1]+j*nxd[2]+k]-0.5,2)/0.005);
            }
        }
    }
}




