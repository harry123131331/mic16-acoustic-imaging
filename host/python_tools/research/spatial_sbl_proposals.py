"""Isolated SRP-SBL-inspired proposal frontend, not a source detector.

Lai et al. 2024 Eq.16 supplies the multi-dictionary update. Here frequency
bins are grouped into the original 24 zones; that is an approximation whose
acoustic value is unproven. PHAT mixtures are not strictly linear mixtures of
single-source responses. Neither source class, count nor true DOA enters APIs.
"""
import numpy as np
from scipy.linalg import cho_factor, cho_solve


def shared_gamma_step(dictionaries, observations, noise_variance, gamma):
    """Eq.16, using real zone dictionaries and Cholesky solves, no inverses.

    D: [zone,N,Q], Z: [zone,N,T]. gamma_q(new) = gamma_q(old)/T
    * sum_z ||Z_z.T Sigma_z^-1 d_zq||^2 / sum_z d_zq.T Sigma_z^-1 d_zq.
    The paper's gamma_n(old) index is interpreted as the corresponding q.
    """
    D=np.asarray(dictionaries,np.float64);Z=np.asarray(observations,np.float64)
    var=np.asarray(noise_variance,np.float64);g=np.asarray(gamma,np.float64)
    k,n,q=D.shape
    assert Z.shape[:2]==(k,n) and var.shape==(k,) and g.shape==(q,)
    assert Z.shape[-1]>0 and (var>0).all() and (g>=0).all()
    assert all(np.isfinite(x).all() for x in (D,Z,var,g))
    numerator=np.zeros(q);denominator=np.zeros(q);objective=0.
    eye=np.eye(n);T=Z.shape[-1]
    for z in range(k):
        dz=D[z];covariance=(dz*g)@dz.T+var[z]*eye
        factor=cho_factor(covariance,lower=True,check_finite=False)
        solved_D=cho_solve(factor,dz,check_finite=False)
        projected=Z[z].T@solved_D
        numerator+=np.sum(projected**2,axis=0)
        denominator+=np.sum(dz*solved_D,axis=0)
        solved_Z=cho_solve(factor,Z[z],check_finite=False)
        objective+=.5*(T*2*np.log(np.diag(factor[0])).sum()+np.sum(Z[z]*solved_Z))
    ratio=np.divide(numerator,denominator,out=np.zeros(q),where=denominator>0)
    new=g*ratio/T
    if not np.isfinite(new).all() or np.any(new<0):
        raise FloatingPointError('invalid gamma update: stop without adaptive rescue')
    return new,float(objective)


def solve_shared_gamma(dictionaries,observations,noise_variance,maximum_iterations=20,relative_tolerance=1e-3):
    D=np.asarray(dictionaries,np.float64);Z=np.asarray(observations,np.float64)
    # 固定矩初始化：在均匀先验下，用各方向观测方差
    # 减去实测独立参考方差。
    denominator=float(np.mean(np.sum(D**2,axis=-1)))
    signal=max(float(np.mean(Z**2)-np.mean(noise_variance)),0.)
    initial=max(signal/max(denominator,1e-30),1e-12)
    gamma=np.full(D.shape[-1],initial);history=[]
    for iteration in range(1,maximum_iterations+1):
        new,objective=shared_gamma_step(D,Z,noise_variance,gamma)
        relative=float(np.linalg.norm(new-gamma)/max(float(np.linalg.norm(gamma)),1e-30))
        history.append(dict(iteration=iteration,relative_gamma_change=relative,
            negative_log_evidence_before_update=objective,max_gamma=float(new.max()),sum_gamma=float(new.sum())))
        gamma=new
        if relative<=relative_tolerance:break
    return gamma,dict(initial_uniform_gamma=initial,iterations=len(history),
        converged=history[-1]['relative_gamma_change']<=relative_tolerance,history=history)


class SpatialSBLProposals:
    """325-grid dictionary and 91 coarse observations; independent of labels."""
    def __init__(self,projector):
        from audit_pre_gate_candidate_coverage import fixed_bounded_pool
        self._bounded_pool=fixed_bounded_pool
        aa,ee=np.meshgrid(projector.grid.azimuths_deg,projector.grid.elevations_deg)
        self.directions=np.c_[aa.ravel(),ee.ravel()].astype(np.float64)
        self.coarse_indices=np.arange(325).reshape(13,25)[::2,::2].ravel()
        assert projector.grid.shape==(13,25) and len(self.coarse_indices)==91 and len(projector.zone_slices)==24
        dictionaries=[]
        for zone in projector.zone_slices:
            fine=projector.signatures[:,:,zone].reshape(325,-1).astype(np.complex128)
            coarse=fine[self.coarse_indices]
            dictionaries.append((coarse.conj()@fine.T).real/fine.shape[-1])
        self.dictionaries=np.stack(dictionaries)
        assert self.dictionaries.shape==(24,91,325) and np.isfinite(self.dictionaries).all()

    def propose(self,maps,quiet_maps):
        observed=np.asarray(maps,np.float64);quiet=np.asarray(quiet_maps,np.float64)
        assert observed.shape==(20,24,13,25) and quiet.ndim==4 and quiet.shape[1:]==(24,13,25)
        assert len(quiet)>1 and np.isfinite(observed).all() and np.isfinite(quiet).all()
        Z=observed.reshape(20,24,325)[:,:,self.coarse_indices].transpose(1,2,0)
        reference=quiet.reshape(len(quiet),24,325)[:,:,self.coarse_indices].transpose(1,2,0)
        noise_variance=np.maximum(np.var(reference,axis=(1,2),ddof=1),1e-6)
        gamma,diagnostics=solve_shared_gamma(self.dictionaries,Z,noise_variance)
        order=np.argsort(-gamma,kind='stable')
        raw=[dict(direction=self.directions[j].tolist(),grid_index=int(j),gamma=float(gamma[j]))
            for j in order if gamma[j]>0]
        pool=self._bounded_pool(raw)
        diagnostics.update(noise_variance=noise_variance.tolist(),noise_floor=1e-6,
            maximum_iterations=20,relative_tolerance=1e-3,proposal_NMS_deg=10.,proposal_cap=16,
            dictionary_bytes=self.dictionaries.nbytes,coarse_directions=91,fine_directions=325)
        return dict(pool=pool,gamma=gamma,diagnostics=diagnostics)
