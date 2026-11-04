"""Colored diffuse/sensor background via frequency-domain coherence mixing.

Habets, Cohen and Gannot, JASA2008, doi:10.1121/1.2987429.
Gamma_ij(f)=q*sinc(2*f*d_ij/c)+(1-q)*I. NumPy sinc includes pi.
This homogeneous Gaussian approximation has no individually labeled point DOA.
"""
import numpy as np
from scipy.signal import get_window


class SpatialBackground:
    def __init__(self, microphone_positions, fs=39062.5, c=343., diffuse_fraction=.9,
                 ar1=.8, fft_size=2048):
        p=np.asarray(microphone_positions,float)
        if p.shape!=(16,3) or not np.isfinite(p).all():
            raise ValueError('finite16x3 microphone positions required')
        if not 0<=diffuse_fraction<1 or not abs(ar1)<1 or fft_size!=2048:
            raise ValueError('positive sensor floor, stable color filter and2048FFT required')
        self.fs=fs;self.c=c;self.nfft=fft_size;self.hop=fft_size//2
        self.q=diffuse_fraction;self.ar1=ar1
        self.distances=np.linalg.norm(p[:,None,:]-p[None,:,:],axis=-1)
        self.frequencies=np.fft.rfftfreq(fft_size,1/fs)
        self.gamma=self.target(self.frequencies)
        self.factor=np.linalg.cholesky(self.gamma)
        self.factor_reconstruction_max_error=float(np.max(np.abs(
            self.factor@self.factor.transpose(0,2,1)-self.gamma)))
        self.color=1/np.abs(1-ar1*np.exp(-2j*np.pi*self.frequencies/fs))
        self.window=np.sqrt(get_window('hann',fft_size,fftbins=True))

    def target(self, frequencies):
        f=np.asarray(frequencies,float)
        return self.q*np.sinc(2*f[:,None,None]*self.distances/self.c)+(1-self.q)*np.eye(16)[None]

    def generate(self, sample_count, rms, seed):
        if type(sample_count) is not int or sample_count<1 or not 0<rms<1:
            raise ValueError('positive sample count and digital RMS required')
        blocks=int(np.ceil(sample_count/self.hop))+2
        rng=np.random.default_rng(seed)
        white=rng.standard_normal((blocks,self.nfft,16))
        spectrum=np.fft.rfft(white,axis=1)
        mixed=np.einsum('kij,bkj->bki',self.factor,spectrum,optimize=True)
        mixed*=self.color[None,:,None]
        time=np.fft.irfft(mixed,n=self.nfft,axis=1)
        output=np.zeros(((blocks-1)*self.hop+self.nfft,16),float)
        for b in range(blocks):
            start=b*self.hop
            output[start:start+self.nfft]+=time[b]*self.window[:,None]
        output=output[self.hop:self.hop+sample_count]
        # 公共标量保持空间相干性和通道比例。
        output*=rms/np.sqrt(np.mean(output*output))
        return output.astype(np.float32)
