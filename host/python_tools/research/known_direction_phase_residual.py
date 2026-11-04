"""Known-direction PHAT observation projection, operator-only prototype.

The caller supplies observed cross spectra and geometry-derived signatures.
No labels, sound classes, source count, peak search or acceptance gate here.
Orthogonal projection removes a fitted component in PHAT feature space;
it is not exact waveform/source separation or a reproduction of SVD-PHAT.
"""
import numpy as np
VERSION='known_direction_original_phase_projection_operator_20261014'

def project_known_components(observations,known_signatures):
    x=np.asarray(observations,np.complex128);a=np.asarray(known_signatures,np.complex128)
    if x.ndim!=2 or a.ndim!=2 or a.shape[1]!=x.shape[1] or not 1<=len(x)<=20 or x.shape[1]<1 or len(a)>8:
        raise ValueError('observations[T<=20,D],known_signatures[K<=8,D] required')
    if not np.isfinite(x).all() or not np.isfinite(a).all():raise ValueError('finite observations and signatures required')
    residual=x.copy();basis=[];ignored=[]
    for j,atom in enumerate(a):
        scale=float(np.linalg.norm(atom))
        if scale<=1e-12:ignored.append(j);continue
        unit=atom/scale
        # 用两次正交化避免相近/重复特征导致正交性丢失。
        for _ in range(2):
            for u in basis:unit-=u*np.vdot(u,unit)
        length=float(np.linalg.norm(unit))
        if length<=1e-10:ignored.append(j);continue
        basis.append(unit/length)
    if basis:
        u=np.stack(basis)
        residual-=(x@u.conj().T)@u
        orthogonality=float(np.max(np.abs(residual@u.conj().T)))
    else:orthogonality=0.
    initial=np.linalg.norm(x,axis=1);final=np.linalg.norm(residual,axis=1)
    assert np.all(final<=initial+1e-10*np.maximum(initial,1.))
    return residual,dict(operator_rank=len(basis),known_atoms_supplied=len(a),ignored_atom_indices=ignored,
        original_norm=initial.tolist(),residual_norm=final.tolist(),maximum_residual_known_component=orthogonality,
        normalized_after_projection=False,source_detection_or_stop_decision=False)
