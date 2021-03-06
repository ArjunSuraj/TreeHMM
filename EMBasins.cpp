//--------------------------------------------
//  EMBasins.cpp
//
//  Created by Jason Prentice on November 13, 2012.
//  Modified by Adrianna Loback.
//  Mexed version checked for local use
//  on December 8, 2018.
//
//--------------------------------------------

#include "EMBasins.h"
#include "BasinModel.h"
#include "TreeBasin.h"

// Choose either MATLAB or PYTHON to link to via Boost
//#define MATLAB
#define PYTHON

#ifdef MATLAB
#include "matrix.h"
#include "mex.h"
// Comment this if you want just EMBasins (i.e. no temporal correlations)
// Leave uncommented if you want HMM i.e. temporal correlations
// Only applies to Matlab as it allows binding only one function named mexFunction,
//  whereas python allows separate bindings for pyHMM and pyEMBasins, etc.
#define matlabHMM
// See below: typedef <...> BasinType for spatial correlations
#endif

#include <queue>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <exception>



// Selects which basin model to use -- one of the two below
typedef TreeBasin BasinType;
//typedef IndependentBasin BasinType;

#ifdef MATLAB

template <typename T>
void writeOutputMatrix(int pos, vector<T> value, int N, int M, mxArray**& plhs) {
    mxArray* out_matrix = mxCreateDoubleMatrix(N,M,mxREAL);
    double* pr = mxGetPr(out_matrix);
    for (typename vector<T>::iterator it=value.begin(); it!=value.end(); ++it) {
        *pr++ = (double) *it;
    }
    plhs[pos] = out_matrix;
    return;
}

void writeOutputStruct(int pos, vector<paramsStruct>& value, mxArray**& plhs) {
//    for (int n=0; n<value.size(); n++) {
    mxArray* out_struct = mxCreateStructMatrix(value.size(),1,value[0].get_nfields(), value[0].fieldNamesArray());
    int n = 0;
    for (vector<paramsStruct>::iterator it = value.begin(); it != value.end(); ++it) {
        for (int i=0; i < it->get_nfields(); i++) {
            vector<double>* data = it->getFieldData(i);
            
            int N = it->getFieldN(i);
            int M = it->getFieldM(i);
            mxArray* field_data = mxCreateDoubleMatrix(N, M, mxREAL);
            double* pr = mxGetPr(field_data);
            double* iter_pr = pr;
            for (vector<double>::iterator data_it=data->begin(); data_it != data->end(); ++data_it) {
                *iter_pr++ = *data_it;
            }            
            mxSetFieldByNumber(out_struct, n, i, field_data);
        }
        n++;
    }
    plhs[pos] = out_struct;
    return;
}

#endif

vector<double> mpow(vector<double>& matrix, int n, int k) {

    if (k==1) {
        return matrix;
    } else if (k==0) {
        vector<double> ident (n*n,0);
        for (int i=0; i<n; i++) {
            ident[n*i + i] = 1;
        }
        return ident;
    } else {
        vector<double> ml = mpow(matrix, n, floor(k/2));
        vector<double> mr = mpow(matrix, n, k - floor(k/2));
        vector<double> mout (n*n);
        for (int i=0; i<n; i++) {
            for (int j=0; j<n; j++) {
                for (int k=0; k<n; k++) {
                    mout[n*i+j] += ml[n*k + j] * mr[n*i + k];
                }
            }
        }
        return mout;        
    }

}

#ifdef MATLAB

void mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[]) {
 // #ifdef matlabHMM, then this function uses temporal correlations, i.e. uses HMM(),
 // and signature from matlab is as below
 // [freq,w,m,P,logli,prob] = EMBasins(st, unobserved_edges, binsize, nbasins, niter)
 // #ifndef matlabHMM, then this function doesn't use temporal correlations, i.e. uses EMBasins(),
 // and signature from matlab is as below
 // [freq,w,m,P,logli,prob] = EMBasins(st, st_test, unobserved_edges, binsize, nbasins, niter)

    cout << "Reading inputs..." << endl;
    int N = mxGetNumberOfElements(prhs[0]);
    vector<vector<double> > st (N);    
    for (int i=0; i<N; i++) {
        mxArray* elem = mxGetCell(prhs[0], i);
        double* elem_pr = mxGetPr(elem);
        int nspikes = mxGetNumberOfElements(elem);
        for (int n=0; n<nspikes; n++) {
            st[i].push_back(elem_pr[n]);
        }
    }

#ifdef matlabHMM
    int n_unobserved_blocks = mxGetM(prhs[1]);
    vector<double> unobserved_edges_low (n_unobserved_blocks);
    vector<double> unobserved_edges_high (n_unobserved_blocks);
    if (n_unobserved_blocks > 0 && mxGetN(prhs[1]) != 2) {
        cerr << "Unobserved edges must be empty or have two columns." << endl;
    } else {
        double* unobserved_edges_pr = mxGetPr(prhs[1]);
        for (int n=0; n<n_unobserved_blocks; n++) {
            unobserved_edges_low[n] = unobserved_edges_pr[n];
            unobserved_edges_high[n] = unobserved_edges_pr[n_unobserved_blocks + n];
        }
    }
#endif

#ifndef matlabHMM
    int N1 = mxGetNumberOfElements(prhs[1]);
    vector<vector<double> > st_test (N1);    
    for (int i=0; i<N1; i++) {
        mxArray* elem = mxGetCell(prhs[1], i);
        double* elem_pr = mxGetPr(elem);
        int nspikes = mxGetNumberOfElements(elem);
        for (int n=0; n<nspikes; n++) {
            st_test[i].push_back(elem_pr[n]);
        }
    }
#endif

    double binsize = *mxGetPr(prhs[2]);
    int nbasins = (int) *mxGetPr(prhs[3]);
    int niter = (int) *mxGetPr(prhs[4]);    

  /*
    // Autocorrelation model
    Autocorr<BasinType> basin_obj(st, binsize, nbasins);
    vector<double> logli = basin_obj.train(niter);
    cout << "Viterbi..." << endl;
    vector<int> alpha = basin_obj.viterbi();
//    cout << "P...." << endl;
//    vector<double> P = basin_obj.get_P();
//    cout << "Pred prob..." << endl;
//    pair<vector<double>, vector<double> > tmp = basin_obj.pred_prob();
//    vector<double> pred_prob = tmp.first;
//    vector<double> hist = tmp.second;
    //    vector<unsigned long> hist = basin_obj.state_hist();
//    int T = floor(P.size() / nbasins);
    int T = alpha.size();
    
    cout << "Params..." << endl;
    vector<paramsStruct> params = basin_obj.basin_params();
    
    writeOutputMatrix(0, logli, niter, 1, plhs);
//    writeOutputMatrix(1, basin_obj.get_trans(), nbasins, nbasins, plhs);
    //    writeOutputMatrix(2, P, nbasins, T, plhs);
//    writeOutputMatrix(2, basin_obj.emiss_prob(), nbasins, T, plhs);
    //    cout << "Microstates..." << endl;
    //    writeOutputMatrix(2, basin_obj.state_v_time(), 1, T, plhs);
    writeOutputMatrix(1, alpha, T, 1, plhs);
//    writeOutputMatrix(4, pred_prob, 1, pred_prob.size(), plhs);
//    writeOutputMatrix(5, hist, 1, hist.size(), plhs);
    writeOutputStruct(2, params, plhs);
    writeOutputMatrix(3, basin_obj.get_forward(), nbasins,T,plhs);
    writeOutputMatrix(4, basin_obj.get_backward(), nbasins,T,plhs);
    writeOutputMatrix(5, basin_obj.get_basin_trans(), 4*N,nbasins,plhs);
    writeOutputMatrix(6, basin_obj.w, nbasins,1,plhs);
    writeOutputMatrix(7, basin_obj.P_indep(), nbasins, T, plhs);
//    cout << "Samples..." << endl;
//    writeOutputMatrix(3, basin_obj.sample(100000), N,100000, plhs);
    */
    
#ifdef matlabHMM
    // Hidden Markov model
    HMM<BasinType> basin_obj(st, unobserved_edges_low, unobserved_edges_high, binsize, nbasins);
    vector<double> train_logli;
    vector<double> test_logli;
    tie(train_logli,test_logli) = basin_obj.train(niter);
    cout << "Viterbi..." << endl;
    vector<int> alpha = basin_obj.viterbi(true);
    cout << "P...." << endl;
    vector<double> P = basin_obj.get_P();
    cout << "Pred prob..." << endl;
    pair<vector<double>, vector<double> > tmp = basin_obj.pred_prob();
    vector<double> pred_prob = tmp.first;
    vector<double> hist = tmp.second;
//    vector<unsigned long> hist = basin_obj.state_hist();
    int T = floor(P.size() / nbasins);

    cout << "Params..." << endl;
    vector<paramsStruct> params = basin_obj.basin_params();

    cout << "Writing outputs..." << endl;    
    // NOTE: matlab uses column-major while C/C++ uses row-major order
    // Thus, call as writeOutputMatrix(plhs_index, Cmatrix, Ccolumn, Crow, plhs)
    // Ccolumn then Crow not the other way around!
    writeOutputStruct(0, params, plhs);
    writeOutputMatrix(1, basin_obj.get_trans(), nbasins, nbasins, plhs);
    writeOutputMatrix(2, P, nbasins, T, plhs);
    writeOutputMatrix(3, basin_obj.emiss_prob(), nbasins, T, plhs);
    //cout << "Microstates..." << endl;
    // state_v_time() seg faults, see further notes in the function
    //writeOutputMatrix(2, basin_obj.state_v_time(), 1, T, plhs);
    writeOutputMatrix(4, alpha, T, 1, plhs);
    writeOutputMatrix(5, pred_prob, 1, pred_prob.size(), plhs);
    writeOutputMatrix(6, hist, 1, hist.size(), plhs);
    //cout << "Samples..." << endl;
    writeOutputMatrix(7, basin_obj.sample(100000), N,100000, plhs);
    // word_list() gave error sometimes, when this was called from python,
    //  dunno how it'll behave from matlab
//    writeOutputMatrix(7, basin_obj.word_list(), N, hist.size(), plhs);
    writeOutputMatrix(8, basin_obj.stationary_prob(), 1,nbasins, plhs);
    writeOutputMatrix(9, train_logli, niter, 1, plhs);
    writeOutputMatrix(10, test_logli, niter, 1, plhs);
#endif    
    
#ifndef matlabHMM
    // Mixture model
    cout << "Initializing EM..." << endl;
    EMBasins<BasinType> basin_obj(st, st_test, binsize, nbasins);
    
    cout << "Training model..." << endl;
    vector<double> logli;
    vector<double> test_logli;
    tie(logli,test_logli) = basin_obj.train(niter);
    //vector<double> test_logli = basin_obj.test_logli;
    
//    cout << "Testing..." << endl;
//    vector<double> P_test = basin_obj.test(st_test,binsize);
    
    vector<paramsStruct> params = basin_obj.basin_params();
    int nstates = basin_obj.nstates();
    int nstates_test = basin_obj.nstates_test();
    cout << nstates << " states." << endl;
    
//    cout << "Getting samples..." << endl;
    int nsamples = 100000;
    vector<char> samples = basin_obj.sample(nsamples);


    cout << "Writing outputs..." << endl;    
    
    // NOTE: matlab uses column-major while C/C++ uses row-major order
    // Thus, call as writeOutputMatrix(plhs_index, Cmatrix, Ccolumn, Crow, plhs)
    // Ccolumn then Crow not the other way around!
    writeOutputStruct(0, params, plhs);
    writeOutputMatrix(1, basin_obj.w, nbasins,1, plhs);    
    writeOutputMatrix(2, samples, N, nsamples, plhs);
    writeOutputMatrix(3, basin_obj.word_list(), N, nstates, plhs);
    writeOutputMatrix(4, basin_obj.state_hist(), nstates, 1, plhs);
    writeOutputMatrix(5, basin_obj.word_list_test(), N, nstates_test, plhs);
    writeOutputMatrix(6, basin_obj.test_hist(), nstates_test, 1, plhs);
    writeOutputMatrix(7, basin_obj.P(), nbasins, nstates, plhs);
    writeOutputMatrix(8, basin_obj.P_test(), nbasins, nstates_test, plhs);
    writeOutputMatrix(9, basin_obj.all_prob(), nstates, 1, plhs);
    writeOutputMatrix(10, basin_obj.test_prob(), nstates_test, 1, plhs);
    writeOutputMatrix(11, logli, niter, 1, plhs);
    writeOutputMatrix(12, test_logli, niter, 1, plhs);
//    writeOutputMatrix(6, P_test, nbasins, P_test.size()/nbasins, plhs);

#endif    
    
    /*
    // k-fold cross-validation
    int kfolds = 10;
    vector<double> logli = basin_obj.crossval(niter, kfolds);
    
    writeOutputMatrix(0, logli, niter, kfolds, plhs);
    */
    
    
    return;
}

#endif

#ifdef PYTHON

#include<boost/python.hpp>
// numpy.hpp needs boost >= 1.63.0, on IST cluster: `module load boost` to get 1.70.0
#include<boost/python/numpy.hpp>

namespace py = boost::python;
namespace np = boost::python::numpy;

template <typename T>
np::ndarray writePyOutputMatrix(vector<T> value, int rows, int cols) {
// convert C++ vector / 2D vector to a Python numpy array of rows x cols
// be sure to only pass vector<double> or vector<vector<double>>
//  since double size is hard-coded below!
    
    // https://www.boost.org/doc/libs/1_71_0/libs/python/doc/html/numpy/tutorial/ndarray.html
    np::dtype dt = np::dtype::get_builtin<double>();    // double hard-coded

    //py::tuple shape = py::make_tuple(value.size());    // size gives only rows
    py::tuple shape = py::make_tuple(rows,cols);

    //py::tuple stride = py::make_tuple(sizeof(double));
    //py::object own;
    // https://www.boost.org/doc/libs/1_71_0/libs/python/doc/html/numpy/tutorial/fromdata.html
    // from_data uses the same location for both arrays in C and python
    //np::ndarray arr = np::from_data (value,dt,shape,stride,own);
    
    np::ndarray arr = np::zeros(shape, dt);
    // https://stackoverflow.com/questions/10701514/how-to-return-numpy-array-from-boostpython
    // double hard-coded
    std::copy(value.begin(), value.end(), reinterpret_cast<double*>(arr.get_data()));
    return arr;
}

py::list writePyOutputStruct(vector<paramsStruct>& value) {
    py::list outstruct;
    int n = 0;
    for (vector<paramsStruct>::iterator it = value.begin(); it != value.end(); ++it) {
        for (int i=0; i < it->get_nfields(); i++) {
            vector<double>* data = it->getFieldData(i);
            int N = it->getFieldN(i);
            int M = it->getFieldM(i);
            np::dtype dt = np::dtype::get_builtin<double>();    // double hard-coded
            py::tuple shape = py::make_tuple(N,M);
            np::ndarray arr = np::zeros(shape, dt);
            // double hard-coded
            std::copy(data->begin(), data->end(), reinterpret_cast<double*>(arr.get_data()));
            outstruct.append(arr);
        }
        n++;
    }
    return outstruct;
}

py::list writePyOutputStructDict(vector<paramsStruct>& value) {
    py::list outlistdict;
    int n = 0;
    for (vector<paramsStruct>::iterator it = value.begin(); it != value.end(); ++it) {
        py::dict outstruct;
        for (int i=0; i < it->get_nfields(); i++) {
            vector<double>* data = it->getFieldData(i);
            int N = it->getFieldN(i);
            int M = it->getFieldM(i);
            np::dtype dt = np::dtype::get_builtin<double>();    // double hard-coded
            py::tuple shape = py::make_tuple(N,M);
            np::ndarray arr = np::zeros(shape, dt);
            // double hard-coded
            std::copy(data->begin(), data->end(), reinterpret_cast<double*>(arr.get_data()));
            outstruct[it->getFieldName(i)] = arr;
        }
        n++;
        outlistdict.append(outstruct);
    }
    return outlistdict;
}

vector<vector<double>> getSpikeTimes(py::list nrnspiketimes) {
    int N = len(nrnspiketimes); 
    vector<vector<double>> st (N);
    for (int i=0; i<N; i++) {
        py::list spiketimes = py::extract<py::list>(nrnspiketimes[i]);
        int nspikes = len(spiketimes);
        
        for (int n=0; n<nspikes; n++) {
          
          

            double a = static_cast<double>(py::extract<int>(spiketimes[n]));
            st[i].push_back(a);
           
             
        }
      
    }
  
    return st;
}

vector<double> getVec(np::ndarray arr) {
    int input_size = arr.shape(0);
    std::vector<double> v(input_size);
    for (int i=0; i<input_size; ++i)
        v[i] = py::extract<long>(arr[i]);
    return v;
}

py::list pyEMBasins(py::list nrnspiketimes, py::list nrnspiketimes_test, double binsize, int nbasins, int niter) {
// params,w,samples,state_hist,P,prob,logli,P_test = pyEMBasins(spiketimes, spiketimes_test, binsize, nbasins, niter)
 
// nrnspiketimes is a list of lists, nNeurons x nSpikeTimes (# of spike times is different for each neuron, so not an array
// binsize is the number of samples per bin, @ 10KHz sample rate and a 20ms bin, binsize=200
// nbasins is number of modes, around 70 for the data in Prentice et al 2016 for HMM & TreeBasin
// niter is the number of times EM is repeated

    // https://www.boost.org/doc/libs/1_71_0/libs/python/doc/html/reference/index.html
    // see: https://www.boost.org/doc/libs/1_71_0/libs/python/doc/html/reference/object_wrappers/boost_python_list_hpp.html
    //cout << py::len(st) << py::extract<double>(st[0]) << endl;

    // https://www.boost.org/doc/libs/1_71_0/libs/python/doc/html/numpy/tutorial/simple.html
    // Initialise the Python runtime, and the numpy module. Failure to call these results in segmentation errors!
    Py_Initialize();
    np::initialize();
  
    cout << "Reading inputs..." << endl;
     
    int N = len(nrnspiketimes); 
    vector<vector<double>> st = getSpikeTimes(nrnspiketimes);
    vector<vector<double>> st_test = getSpikeTimes(nrnspiketimes_test);

    // Mixture model
    cout << "Initializing EM..." << endl;
    EMBasins<BasinType> basin_obj(st, st_test, binsize, nbasins);
        
    cout << "Training model..." << endl;
    vector<double> logli;
    vector<double> test_logli;
    tie(logli,test_logli) = basin_obj.train(niter);
    //vector<double> test_logli = basin_obj.test_logli;
    
    // Aditya modified: I've added testing at each iter in train()
    //  so no need of testing at the end of training
    //cout << "Testing..." << endl;
    //vector<double> P_test;
    //double logli_test;
    // ::test() returns P_test as nbasins x nbins,
    // unlike ::P_test() or P() which return nbasins x npatterns
    //tie(P_test,logli_test) = basin_obj.test(st_test,binsize);
    
    vector<paramsStruct> params = basin_obj.basin_params();
    int nstates = basin_obj.nstates();
    int nstates_test = basin_obj.nstates_test();
    cout << nstates << " states." << endl;
    
//    cout << "Getting samples..." << endl;
    int nsamples = 100000;
    vector<char> samples = basin_obj.sample(nsamples);

    cout << "Writing outputs..." << endl;    
    py::list outlist = py::list();
    
    // NOTE: python uses row-major order just like C/C++, and unlike matlab
    // Thus, call as writePyOutputMatrix(Cmatrix, Crow, Ccolumn)
    // unlike in the matlab bindings above, where Crow and Ccolumn are flipped    
    outlist.append(writePyOutputStructDict(params));
    outlist.append(writePyOutputMatrix(basin_obj.w,1,nbasins));
    outlist.append(writePyOutputMatrix(samples,nsamples,N));
    outlist.append(writePyOutputMatrix(basin_obj.word_list(),nstates,N));
    outlist.append(writePyOutputMatrix(basin_obj.state_hist(),1,nstates));
    outlist.append(writePyOutputMatrix(basin_obj.word_list_test(),nstates_test,N));
    outlist.append(writePyOutputMatrix(basin_obj.test_hist(),1,nstates_test));
    // P and P_test are the Qmodes/Z (cf. my MixtureModel.py calcModePosterior())
    //  for each state in train_states and test_states
    outlist.append(writePyOutputMatrix(basin_obj.P(),nstates,nbasins));
    outlist.append(writePyOutputMatrix(basin_obj.P_test(),nstates_test,nbasins));
    // all_prob and test_prob are the Z's for each state in train_states and test_states
    outlist.append(writePyOutputMatrix(basin_obj.all_prob(),1,nstates));
    outlist.append(writePyOutputMatrix(basin_obj.test_prob(),1,nstates_test));
    outlist.append(writePyOutputMatrix(logli,1,niter));
    outlist.append(writePyOutputMatrix(test_logli,1,niter));
    //outlist.append(logli_test);
   
    // Aditya notes: P and P_test correspond to my Qmodes/Z (see MixtureModel.py calcModePosterior())
    //  dim is nModes x nStates i.e. nbasins here x number of distinct states
    // However, my Qmodes is nModes x ntimesteps
    // Still, can use the state/pattern at each time step to choose its P from this matrix
    // To calculate log likelihood, I need Z at each time step/bin!
    //  (w.Qmodes)/Z != w.P = w.(Qmodes/Z) won't work because P is normalized sum=1 at each time step
   
    return outlist;
}

void pyInit() {
    // https://www.boost.org/doc/libs/1_71_0/libs/python/doc/html/numpy/tutorial/simple.html
    // Initialise the Python runtime, and the numpy module. Failure to call these results in segmentation errors!
    Py_Initialize();
    np::initialize();
    cout << "Initialized python and numpy" << endl;
}

py::list pyHMM(py::list nrnspiketimes, np::ndarray & unobserved_edges_lo, np::ndarray & unobserved_edges_hi, double binsize, int nbasins, int niter) {
// params,w,samples,state_hist,P,prob,logli,P_test = pyEMBasins(spiketimes, spiketimes_test, binsize, nbasins, niter)
 
// nrnspiketimes is a list of lists, nNeurons x nSpikeTimes (# of spike times is different for each neuron, so not an array
// binsize is the number of samples per bin, @ 10KHz sample rate and a 20ms bin, binsize=200
// nbasins is number of modes, around 70 for the data in Prentice et al 2016 for HMM & TreeBasin
// niter is the number of times EM is repeated

    // https://www.boost.org/doc/libs/1_71_0/libs/python/doc/html/reference/index.html
    // see: https://www.boost.org/doc/libs/1_71_0/libs/python/doc/html/reference/object_wrappers/boost_python_list_hpp.html
    //cout << py::len(st) << py::extract<double>(st[0]) << endl;
  
    cout << "Reading inputs..." << endl;
    int N = len(nrnspiketimes);
    vector<vector<double>> st = getSpikeTimes(nrnspiketimes);
    int n_unobserved_blocks = len(unobserved_edges_lo);
    vector<double> unobserved_edges_low (n_unobserved_blocks);
    vector<double> unobserved_edges_high (n_unobserved_blocks);
    if (n_unobserved_blocks>0) {
        unobserved_edges_low = getVec(unobserved_edges_lo);
        unobserved_edges_high = getVec(unobserved_edges_hi);
    }

    // Hidden Markov model
    HMM<BasinType> basin_obj(st, unobserved_edges_low, unobserved_edges_high, binsize, nbasins);
    vector<double> train_logli;
    vector<double> test_logli;
    tie(train_logli,test_logli) = basin_obj.train(niter);
    cout << "Viterbi..." << endl;
    vector<int> alpha = basin_obj.viterbi(true);
    cout << "P...." << endl;
    vector<double> P = basin_obj.get_P();
    cout << "Pred prob..." << endl;
    pair<vector<double>, vector<double> > tmp = basin_obj.pred_prob();
    vector<double> pred_prob = tmp.first;
    vector<double> hist = tmp.second;
//    vector<unsigned long> hist = basin_obj.state_hist();
    int T = floor(P.size() / nbasins);

    cout << "Params..." << endl;
    vector<paramsStruct> params = basin_obj.basin_params();

    cout << "Writing outputs..." << endl;
    py::list outlist = py::list();

    // NOTE: python uses row-major order just like C/C++, and unlike matlab
    // Thus, call as writePyOutputMatrix(Cmatrix, Crow, Ccolumn)
    // unlike in the matlab bindings above, where Crow and Ccolumn are flipped    
    outlist.append(writePyOutputStructDict(params));
    outlist.append(writePyOutputMatrix(basin_obj.get_trans(),nbasins,nbasins));
    outlist.append(writePyOutputMatrix(P,T,nbasins));
    outlist.append(writePyOutputMatrix(basin_obj.emiss_prob(),T,nbasins));
    //cout << "Microstates..." << endl;
    // state_v_time() seg faults, see further notes in the function
    //outlist.append(writePyOutputMatrix(basin_obj.state_v_time(),1,T));
    outlist.append(writePyOutputMatrix(alpha,1,T));
    outlist.append(writePyOutputMatrix(pred_prob,1,pred_prob.size()));
    outlist.append(writePyOutputMatrix(hist,1,hist.size()));
    outlist.append(writePyOutputMatrix(basin_obj.sample(100000),100000,N));
    // don't return basin_obj.word_list(), I got python:free():invalid pointer error
    //  sometimes on `return outlist` on the second call to pyHMM
    //outlist.append(writePyOutputMatrix(basin_obj.word_list(),hist.size(),N));
    outlist.append(writePyOutputMatrix(basin_obj.stationary_prob(),1,nbasins));
    outlist.append(writePyOutputMatrix(train_logli,1,niter));
    outlist.append(writePyOutputMatrix(test_logli,1,niter));
   
    cout << "Returning from C++!" << endl;
    return outlist;
}

BOOST_PYTHON_MODULE(EMBasins)
{
   using namespace boost::python;
   def("pyEMBasins",pyEMBasins);
   def("pyHMM",pyHMM);
   def("pyInit",pyInit);
}

#endif

RNG::RNG() {
    // Initialize mersenne twister RNG
    rng_pr = gsl_rng_alloc(gsl_rng_mt19937);

}
RNG::~RNG() {
    gsl_rng_free(rng_pr);
}
int RNG::discrete(const vector<double>& p) {
    double u = gsl_rng_uniform(rng_pr);
    double c = p[0];
    int ix=0;
    while (c<u) {
        ix++;
        c += p[ix];
    }
    return ix;
}
bool RNG::bernoulli(double p) {
    return (gsl_rng_uniform(rng_pr) < p);
}

vector<int> RNG::randperm(int nmax) {
    vector<int> nvals (nmax);
    for (int i=0; i<nmax; i++) {
        nvals[i] = i;
    }
    for (int i=0; i<nmax; i++) {
        // select random integer ix between i and nmax-1
        // swap i with ix
        int ix = i + gsl_rng_uniform_int(rng_pr, nmax-i);
        int tmp = nvals[i];
        nvals[i] = nvals[ix];
        nvals[ix] = tmp;
    }
    return nvals;
}

template <class BasinT>
EMBasins<BasinT>::EMBasins(int N, int nbasins) : N(N), nbasins(nbasins), w(nbasins) {
    rng = new RNG();
    //srand(time(NULL));
    srand(0);
}


template <class BasinT>
EMBasins<BasinT>::EMBasins(vector<vector<double>>& st, vector<vector<double>>& st_test, double binsize, int nbasins) : nbasins(nbasins), nsamples(0), w(nbasins) {
    
    rng = new RNG();
    
    N = st.size();
    //srand(time(NULL));
    srand(0);

    
    // Build state structure from spike times in st:
    cout << "Building state histogram..." << endl;
    // Dump all the spikes into a vector and sort by spike time
    
    vector<Spike> all_spikes = sort_spikes(st, binsize);
    
    string silent_str (N,'0');

    // Add silent state with frequency of zero
    State this_state;
    string this_str = silent_str;
    this_state.freq = 0;
    this_state.P.assign(nbasins, 0);
    this_state.weight.assign(nbasins, 0);
    this_state.word.assign(N,0);
    all_states.insert(pair<string,State> (silent_str,this_state));
    test_states.insert(pair<string,State> (silent_str,this_state));
    
    int curr_bin = 0;
    for (vector<Spike>::iterator it=all_spikes.begin(); it!=all_spikes.end(); ++it) {

        int next_bin = it->bin;
        int next_cell = it->neuron_ind;
      
        if (next_bin > curr_bin) {
            // Add new state; if it's already been discovered increment its frequency
            this_state.active_constraints = BasinT::get_active_constraints(this_state);

            raster.push_back(this_str);
            
            
            pair<state_iter, bool> ins = all_states.insert(pair<string,State> (this_str,this_state));
            if (!ins.second) {
                (((ins.first)->second).freq)++;
            }
            
            // All states between curr_bin and next_bin (exclusive) are silent; update frequency of silent state accordingly
            for (int i=0; i<(next_bin-curr_bin-1); i++) {
                raster.push_back(silent_str);
            }
            all_states[silent_str].freq += (next_bin - curr_bin - 1);
        
            // Reset state and jump to next bin
            this_str = silent_str;
            this_state.freq = 1;
            this_state.on_neurons.clear();
            this_state.P.assign(nbasins,0);
            this_state.weight.assign(nbasins,0);
            this_state.word.assign(N,0);

            curr_bin = next_bin;            
        }
        
        // Add next_cell to this_state
        if (this_state.word[next_cell] == 0) {  // Don't want to count a cell twice in one bin
            this_str[next_cell] = '1';
            this_state.on_neurons.push_back(next_cell);
            this_state.word[next_cell] = 1;
        }
        
    }

    // Aditya notes: the very last state doesn't get added above, so add it at the end
    // Add new state; if it's already been discovered increment its frequency
    this_state.active_constraints = BasinT::get_active_constraints(this_state);
    raster.push_back(this_str);
    pair<state_iter, bool> ins = all_states.insert(pair<string,State> (this_str,this_state));
    if (!ins.second) {
        (((ins.first)->second).freq)++;
    }

    // if silent state was not present, remove it
    if (all_states[silent_str].freq == 0) {
        all_states.erase(silent_str);
    }
    
    // Now all_states contains all distinct states in the training data together with their frequencies.
    
    for (state_iter it=all_states.begin(); it!=all_states.end(); ++it) {
        nsamples += (it->second).freq;
    }
    
    train_states = all_states;

    // Aditya added notes: I moved this from ::test() to build up test_states
    cout << "Building test states histogram..." << endl;
    
    vector<Spike> test_spikes = sort_spikes(st_test,binsize);

    // Add silent state with frequency of zero
    this_str = silent_str;
    this_state.freq = 0;
    this_state.on_neurons.clear();
    this_state.P.assign(nbasins, 0);
    this_state.weight.assign(nbasins,0);
    this_state.word.assign(N,0);
    
    curr_bin = 0;
    for (vector<Spike>::iterator it=test_spikes.begin(); it!=test_spikes.end(); ++it) {
        
        int next_bin = it->bin;
        int next_cell = it->neuron_ind;
        
        if (next_bin > curr_bin) {
            // Add new state; if it's already been discovered increment its frequency
            this_state.active_constraints = BasinT::get_active_constraints(this_state);
            pair<state_iter, bool> ins = test_states.insert(pair<string,State> (this_str,this_state));
            if (!ins.second) {
                (((ins.first)->second).freq)++;
            }
            
            // All states between curr_bin and next_bin (exclusive) are silent; update frequency of silent state accordingly
            test_states[silent_str].freq += (next_bin - curr_bin - 1);
            
            // unlike for the train bins above, the test bins are not pushed to 'raster'
            
            // Reset state and jump to next bin
            this_str = silent_str;
            this_state.freq = 1;
            this_state.on_neurons.clear();
            this_state.P.assign(nbasins,0);
            this_state.weight.assign(nbasins,0);
            this_state.word.assign(N,0);
            
            curr_bin = next_bin;
        }
        
        // Add next_cell to this_state
        if (this_state.word[next_cell] == 0) {  // Don't want to count a cell twice in one bin
            this_str[next_cell] = '1';
            this_state.on_neurons.push_back(next_cell);
            this_state.word[next_cell] = 1;
        }
        
    }

    // Aditya notes: the very last state doesn't get added above, so add it at the end
    // Add new state; if it's already been discovered increment its frequency
    this_state.active_constraints = BasinT::get_active_constraints(this_state);
    raster.push_back(this_str);
    ins = test_states.insert(pair<string,State> (this_str,this_state));
    if (!ins.second) {
        (((ins.first)->second).freq)++;
    }

    // if silent state was not present above, remove it
    if (test_states[silent_str].freq == 0) {
        test_states.erase(silent_str);
    }

    // Aditya added ends

};

template <class BasinT>
EMBasins<BasinT>::~EMBasins() {
    delete rng;
}


template <class BasinT>
tuple<vector<double>,double> EMBasins<BasinT>::test(const vector<vector<double> >& st, double binsize) {
    
    vector<Spike> all_spikes = sort_spikes(st,binsize);
    int max_bin = all_spikes.back().bin;
    
    vector<double> P_test(nbasins*max_bin);
    map<string, State> eval_states;
    
    
    string silent_str (N,'0');
    
    // Add silent state with frequency of zero
    State this_state;
    string this_str = silent_str;
    this_state.freq = 0;
    this_state.P.assign(nbasins, 0);
    this_state.weight.assign(nbasins,0);
    this_state.word.assign(N,0);

    set_state_P(this_state);
    eval_states.insert(pair<string,State> (silent_str,this_state));
    
    int curr_bin = 0;
    for (vector<Spike>::iterator it=all_spikes.begin(); it!=all_spikes.end(); ++it) {
        
        int next_bin = it->bin;
        int next_cell = it->neuron_ind;
        
        if (next_bin > curr_bin) {
            // Add new state; if it's already been discovered increment its frequency
            this_state.active_constraints = BasinT::get_active_constraints(this_state);
            // Aditya notes: set_state_P sets this_state.P[i]
            //  to Qmodes[i,next_bin]/Z (see MixtureModel.py calcModePosterior())
            //  where i indexes modes, and this_state occurs in next_bin
            set_state_P(this_state);
            pair<state_iter, bool> ins = eval_states.insert(pair<string,State> (this_str,this_state));
            if (!ins.second) {
                (((ins.first)->second).freq)++;
            }
            
            
            // Update probabilities of time bins [curr_bin, next_bin)
            for (int i=0; i<nbasins; i++) {
                // Aditya notes: P_test is Qmodes/Z for this state/bin
                P_test[nbasins*curr_bin + i] = this_state.P[i];
                for (int n=curr_bin+1; n<next_bin; n++) {
                    // Aditya notes: all states between curr_bin and next_bin are silent states,
                    //  set P_test for these intermediate bins to Qmodes/Z for the silent state
                    P_test[nbasins*n + i] = eval_states[silent_str].P[i];
                }
            }
            
            // All states between curr_bin and next_bin (exclusive) are silent; update frequency of silent state accordingly
            eval_states[silent_str].freq += (next_bin - curr_bin - 1);
            
            
            // Reset state and jump to next bin
            this_str = silent_str;
            this_state.freq = 1;
            this_state.on_neurons.clear();
            this_state.P.assign(nbasins,0);
            this_state.weight.assign(nbasins,0);
            this_state.word.assign(N,0);
            
            curr_bin = next_bin;
        }
        
        // Add next_cell to this_state
        if (this_state.word[next_cell] == 0) {  // Don't want to count a cell twice in one bin
            this_str[next_cell] = '1';
            this_state.on_neurons.push_back(next_cell);
            this_state.word[next_cell] = 1;
        }
        
    }
    // Aditya modified begins
    //if (all_states[silent_str].freq == 0) {
    //    all_states.erase(silent_str);
    //}
    // should be eval_states, not all_states I think
    if (eval_states[silent_str].freq == 0) {
        eval_states.erase(silent_str);
    }
    // Aditya modifed ends
    
    // Aditya modified begins
    //return P_test;
    
    test_states = eval_states;
    test_logli = update_P_test();
    return make_tuple(P_test,test_logli);
    // Aditya modified ends
}


template <class BasinT>
vector<double> EMBasins<BasinT>::crossval(int niter, int k) {
    int blocksize = floor(raster.size() / k);
    // Generate random permutation of time bins
    vector<int> tperm = rng->randperm(raster.size());
    vector<double> all_logli (k*niter);
    for (int i=0; i<k; i++) {
    //    populate train_states and test_states
        train_states.clear();
        test_states.clear();
        for (int t=0; t<tperm.size(); t++) {
            string this_str = raster[tperm[t]];
            State this_state = all_states[this_str];
            this_state.freq = 1;
            map<string, State>& curr_map = (t < i*blocksize || t >= (i+1)*blocksize)
                                            ? train_states : test_states;
            pair<state_iter, bool> ins = curr_map.insert(pair<string,State> (this_str,this_state));
            if (!ins.second) {
                (((ins.first)->second).freq)++;
            }
        }
        
    //    train (returns logli of test set)
        vector<double> logli = train(niter);
        for (int j=0; j<niter; j++) {
            all_logli[i*niter + j] = logli[j];
        }
        
    }
    return all_logli;
}

template <class BasinT>
tuple< vector<double>, vector<double> > EMBasins<BasinT>::train(int niter) {

    cout << "Initializing EM params..." << endl;
    w.assign(nbasins,1/(double)nbasins);
    basins.clear();
    // Initialize each basin model
    for (int i=0; i<nbasins; i++) {
        //        BasinT this_basin  BasinT(N));
        basins.push_back(BasinT(N,i,rng));
    }

    update_P();

    test_logli.assign(niter,0);
    vector<double> logli (niter);
    for (int i=0; i<niter; i++) {
        cout << "Iteration " << i << endl;

        // E step

        for (int j=0; j<nbasins; j++) {
            basins[j].reset_stats();
        }
        
        for (state_iter it = train_states.begin(); it!=train_states.end(); ++it) {
            for (int j=0; j<nbasins; j++) {
                basins[j].increment_stats(it->second);
            }
        }
        for (int j=0; j<nbasins; j++) {
            basins[j].normalize_stats();
        }
        
        // M step
//        double alpha = (i<niter/2) ? 1 - (double)i/(niter/2) : 0;
        double alpha = 0.002;
//        if (i >= niter/2) {
//            alpha = 0.002 + (1-0.002)*exp(-(double) (i-niter/2) * (10.0/(((double)(niter/2)-1))));
//        }
//        cout << alpha << endl;
        for (int j=0; j<nbasins; j++) {

            basins[j].doMLE(alpha);
        }
        update_w();

        logli[i] = update_P();
        test_logli[i] = update_P_test();
    }
    return make_tuple(logli,test_logli);
}

template <class BasinT>
void EMBasins<BasinT>::update_w() {
    for (int i=0; i<nbasins; i++) {
        w[i] = basins[i].get_norm() / nsamples;
    }
    
    return;
}

template <class BasinT>
double EMBasins<BasinT>::update_P() {
    
    double logli = 0;
    double norm = 0;
    for (state_iter it=train_states.begin(); it != train_states.end(); ++it) {
        State& this_state = it->second;
        // set_state_P updates this_state.P
        double Z = set_state_P(this_state);
        // Aditya notes: why subtract the running logli here?!
        // this is an online/running mean -- see my explanation in HMM<BasinT>::logli() below
        double delta = log(Z) - logli;
        double f = this_state.freq;
        norm += f;
        // Aditya notes: why /norm within the loop over patterns?!
        // this is an online/running mean -- see my explanation in HMM<BasinT>::logli() below
        logli += (f*delta)/norm;
    }
    // Aditya notes: why not logli = (1/norm) * sum_patterns (freq_pattern*logZ) ?
    // this is an online/running mean -- see my explanation in HMM<BasinT>::logli() below
    return logli;
    
}

// Aditya notes: added this update_P_test similar to training update_P() above
//  the original update_P_test commented below uses log2(Z) instead of log(Z) and gives lower test logli!
template <class BasinT>
double EMBasins<BasinT>::update_P_test() {
    
    double logli = 0;
    double norm = 0;
    // don't use epsilon ~ 10^-16, use min ~ 10^-308
    // see https://en.cppreference.com/w/cpp/types/numeric_limits
    double logmin = log( std::numeric_limits<double>::min() );
    for (state_iter it=test_states.begin(); it != test_states.end(); ++it) {
        State& this_state = it->second;
        // set_state_P updates this_state.P
        double logZ = log( set_state_P(this_state) );
        // Aditya notes: added this else nan in logli
        
        if (std::isinf(logZ)) {
            logZ = logmin;
        }
        // Aditya notes: why subtract the running logli here?!
        // this is an online/running mean -- see my explanation in HMM<BasinT>::logli() below
        double delta = logZ - logli;
        double f = this_state.freq;
        norm += f;
        if (f >= 1) {
            // Aditya notes: why /norm within the loop over patterns?!
            // this is an online/running mean -- see my explanation in HMM<BasinT>::logli() below
            logli += (f*delta)/norm;
        }
    }
    // Aditya notes: why not logli = (1/norm) * sum_patterns (freq_pattern*logZ) ?
    // this is an online/running mean -- see my explanation in HMM<BasinT>::logli() below
    return logli;
    
}

/*
template <class BasinT>
double EMBasins<BasinT>::update_P_test() {
    
    double logli = 0;
    double norm = 0;
    double norm2 = 0;
    double S = 0;
    double Dkl = 0;
    for (state_iter it=test_states.begin(); it != test_states.end(); ++it) {
        State& this_state = it->second;
        double Z = set_state_P(this_state);
        double delta = log2(Z) - logli;
        double f = this_state.freq;
        norm += f;
        if (f >= 1) {
            norm2 += f;
            logli += (f*delta)/norm;
            S += f * log2(f);
            Dkl += f * log2(f/Z);
        }
    }
    Dkl = (Dkl - norm2*log2(norm))/norm;
    S = (S/norm - log2(norm));
//    return Dkl;
    return logli;
    
}
*/


template <class BasinT>
vector<unsigned long> EMBasins<BasinT>::state_hist() const {
    vector<unsigned long> hist (train_states.size(), 0);
    int pos = 0;
    for (const_state_iter it=train_states.begin(); it != train_states.end(); ++it) {
        const State& this_state = it->second;
        hist[pos] = this_state.freq;
        pos++;
    }
    return hist;
}

template <class BasinT>
vector<unsigned long> EMBasins<BasinT>::test_hist() const {
    vector<unsigned long> hist (test_states.size(), 0);
    int pos = 0;
    for (const_state_iter it=test_states.begin(); it != test_states.end(); ++it) {
        const State& this_state = it->second;
        hist[pos] = this_state.freq;
        pos++;
    }
    return hist;
}

template <class BasinT>
vector<double> EMBasins<BasinT>::all_prob() const {
    vector<double> prob (train_states.size(), 0);
    int pos = 0;
    for (const_state_iter it=train_states.begin(); it != train_states.end(); ++it) {
        const State& this_state = it->second;
        prob[pos] = this_state.pred_prob;
        pos++;
    }
    return prob;
    
}

template <class BasinT>
vector<double> EMBasins<BasinT>::test_prob() const {
    vector<double> prob (test_states.size(), 0);
    int pos = 0;
    for (const_state_iter it=test_states.begin(); it != test_states.end(); ++it) {
        const State& this_state = it->second;
        prob[pos] = this_state.pred_prob;
        pos++;
    }
    return prob;
    
}

template <class BasinT>
vector<double> EMBasins<BasinT>::P() const {
    vector<double> P (train_states.size() * nbasins, 0);
    unsigned long pos = 0;
    for (const_state_iter it=train_states.begin(); it != train_states.end(); ++it) {
        const State& this_state = it->second;
        
        for (int i=0; i<nbasins; i++) {
            // this_state.P was already updated on calling update_P() as part of train()
            // update_P() called set_state_P() for each train_state
            P[pos*nbasins + i] = this_state.P[i];
        }
        pos++;
    }
    return P;
}

template <class BasinT>
vector<double> EMBasins<BasinT>::P_test() const {
    vector<double> P_test (test_states.size() * nbasins, 0);
    unsigned long pos = 0;
    for (const_state_iter it=test_states.begin(); it != test_states.end(); ++it) {
        const State& this_state = it->second;
        
        for (int i=0; i<nbasins; i++) {
            // this_state.P was already updated on calling update_P_test as part of train()
            // update_P_test() called set_state_P() for each test_state
            P_test[pos*nbasins + i] = this_state.P[i];
        }
        pos++;
    }
    return P_test;
}


template <class BasinT>
vector<paramsStruct> EMBasins<BasinT>::basin_params() {
//    int nparams = basins[0].nparams();
    vector<paramsStruct> params (nbasins);

    for (int i=0; i<nbasins; i++) {
        params[i] = basins[i].get_params();
    }
    
    return params;
}

template <class BasinT>
vector<char> EMBasins<BasinT>::sample(int nsamples) {
    vector<char> samples (N*nsamples);
    for (int i=0; i<nsamples; i++) {
        int basin_ind = rng->discrete(w);
        vector<char> this_sample = basins[basin_ind].sample();
        // Aditya notes: Careful, the samples "matrix" must be interpreted as (nTimeBins x nNeurons)
        //  samples is represented as a vector here with minor increments (inner loop) for neurons and major (outer loop) for timebins
        //  I've assumed row-major i.e. the C++ default in writePyOutputMatrix
        for (int n=0; n<N; n++) {
            samples[i*N+n] = this_sample[n];
        }
    }
    return samples;
}

template <class BasinT>
vector<Spike> EMBasins<BasinT>::sort_spikes(const vector<vector<double> >& st, double binsize) const {
    vector<Spike> all_spikes;
    for (int i=0; i<N; i++) {
        for (vector<double>::const_iterator it = st[i].begin(); it != st[i].end(); ++it) {
            Spike this_spike;
            double t = (*it);
            this_spike.bin = floor(t/binsize);
            this_spike.neuron_ind = i;
            all_spikes.push_back(this_spike);
        }
    }
    sort(all_spikes.begin(), all_spikes.end(), SpikeComparison());
    return all_spikes;
}


template <class BasinT>
double EMBasins<BasinT>::set_state_P(State& this_state) {
    double Z = 0;
    for (int i=0; i<nbasins; i++) {
        this_state.P[i] = w[i] * basins[i].P_state(this_state);
        Z += this_state.P[i];
    }
    for (int i=0; i<nbasins; i++) {
        this_state.P[i] /= Z;
        this_state.weight[i] = this_state.freq * this_state.P[i];
    }
    this_state.pred_prob = Z;
    return Z;
}

template <class BasinT>
vector<char> EMBasins<BasinT>::word_list() {
    vector<char> out (train_states.size() * N);
    vector<char>::iterator out_it = out.begin();
    for (state_iter it = train_states.begin(); it != train_states.end(); ++it) {
        vector<char> word = (it->second).word;
        for (vector<char>::iterator w_it = word.begin(); w_it!=word.end(); ++w_it) {
            *out_it++ = *w_it;
        }
    }
    return out;
}

template <class BasinT>
vector<char> EMBasins<BasinT>::word_list_test() {
    vector<char> out (test_states.size() * N);
    vector<char>::iterator out_it = out.begin();
    for (state_iter it = test_states.begin(); it != test_states.end(); ++it) {
        vector<char> word = (it->second).word;
        for (vector<char>::iterator w_it = word.begin(); w_it!=word.end(); ++w_it) {
            *out_it++ = *w_it;
        }
    }
    return out;
}


// ************* HMM **********************

template <class BasinT>
HMM<BasinT>::HMM(vector<vector<double> >& st, vector<double> unobserved_l, vector<double> unobserved_u, double binsize, int nbasins) : EMBasins<BasinT> (st.size(),nbasins), w0 (nbasins) {
    
    
    // Build state structure from spike times in st:
    cout << "Building state histogram..." << endl;
    // Dump all the spikes into a vector and sort by spike time
    
    for (vector<double>::iterator it = unobserved_l.begin(); it != unobserved_l.end(); ++it) {
        *it = floor(*it / binsize);
    }
    
    for (vector<double>::iterator it = unobserved_u.begin(); it != unobserved_u.end(); ++it) {
        *it = floor(*it / binsize);
    }
    
    
    vector<Spike> all_spikes = this->sort_spikes(st, binsize); 
    T = all_spikes.back().bin;
    
    forward.assign(T*nbasins, 0);
    backward.assign(T*nbasins, 0); 
    trans.assign(nbasins*nbasins, 0);
//    words.assign(T, "");
    vector<string> words (T, "");
    
    string silent_str (this->N,'0');
    // Add silent state with frequency of zero
    State this_state;
    string this_str = silent_str;
    this_state.freq = 0;
    this_state.P.assign(nbasins, 0);
    this_state.weight.assign(nbasins, 0);
    this_state.word.assign(this->N,0);
    this->all_states.insert(pair<string,State> (silent_str,this_state));
//    test_states.insert(pair<string,State> (silent_str,this_state));
    int curr_bin = 0;
    for (vector<Spike>::iterator it=all_spikes.begin(); it!=all_spikes.end(); ++it) {
        
        int next_bin = it->bin;
        int next_cell = it->neuron_ind;
        
        bool bin_observed = true;
        for (int n=0; n<unobserved_l.size(); n++) {
            if (curr_bin >= unobserved_l[n] && curr_bin < unobserved_u[n]) {
                bin_observed = false;
                break;
            }
        }
        if (next_bin > curr_bin) {
            this->raster.push_back(this_str);
            if (bin_observed) {
                // Add new state; if it's already been discovered increment its frequency
                this_state.active_constraints = BasinT::get_active_constraints(this_state);
                
                pair<state_iter, bool> ins = this->all_states.insert(pair<string,State> (this_str,this_state));
                if (!ins.second) {
                    (((ins.first)->second).freq)++;
                }
            
            }
            // Update probabilities of time bins [curr_bin, next_bin)
            words[curr_bin] = this_str;
            for (int n=curr_bin+1; n<next_bin; n++) {
                words[n] = silent_str;
            }
            
            // All states between curr_bin and next_bin (exclusive) are silent; update frequency of silent state accordingly
//            for (int i=0; i<(next_bin-curr_bin-1); i++) {
//                this->raster.push_back(silent_str);
//            }
//            this->all_states[silent_str].freq += (next_bin - curr_bin - 1);
            for (int t=curr_bin+1; t<next_bin; t++) {
                bool t_observed = true;
                for (int n=0; n<unobserved_l.size(); n++) {
                    if (t >= unobserved_l[n] && t < unobserved_u[n]) {
                        t_observed = false;
                        break;
                    }
                }
                if (t_observed) {
                    (this->all_states[silent_str].freq)++;
                }
                this->raster.push_back(silent_str);
            }
            
            // Reset state and jump to next bin
            this_str = silent_str;
            this_state.freq = 1;
            this_state.on_neurons.clear();
            this_state.P.assign(nbasins,0);
            this_state.weight.assign(nbasins,0);
            this_state.word.assign(this->N,0);
            
            curr_bin = next_bin;
        }
        // Add next_cell to this_state
        if (this_state.word[next_cell] == 0) {  // Don't want to count a cell twice in one bin
            this_str[next_cell] = '1';
            this_state.on_neurons.push_back(next_cell);
            this_state.word[next_cell] = 1;
        }
        
    }
    if (this->all_states[silent_str].freq == 0) {
        this->all_states.erase(silent_str);
    }
    
    // Now all_states contains all states found in the data together with their frequencies.
    int identifier = 0;
    for (state_iter it=this->all_states.begin(); it!=this->all_states.end(); ++it) {
        this->nsamples += (it->second).freq;
        (it->second).identifier = identifier;
        identifier++;
    }
    this->train_states = this->all_states;
    state_list.assign(T, NULL);
    for (int t=0; t<T; t++) {
        bool t_observed = true;
        for (int n=0; n<unobserved_l.size(); n++) {
            if (t >= unobserved_l[n] && t < unobserved_u[n]) {
                t_observed = false;
                break;
            }
        }
        if (t_observed) {
            State& this_state = this->train_states.at(words[t]);
            state_list[t] = &this_state;
        } else {
            state_list[t] = 0;
        }
    }
    
    // Cross validation:
    //tskip = 2;
    
    // Full:
    tskip = 1;

}

template <class BasinT>
vector<int> HMM<BasinT>::state_v_time() {
    vector<int> states (T);
    for (int t=0; t<T; t++) {
        // Aditya notes: in the first iter i.e. t=0,
        //  the line below produces a seg fault
        //  I suppose state_list[0]->identifier is not defined?
        //  Indeed where ever state_list[...] in used in other functions,
        //   it is first checked for validity: if (state_list[i]) { ...
        states[t] = state_list[t]->identifier;
    }
    return states;
}
                      
template <class BasinT>
vector<double> HMM<BasinT>::emiss_prob() {
    vector<double> emiss (this->nbasins * T);
    for (int t=0; t<T; t++) {
//        State& this_state = this->train_states[words[t]];
//        State& this_state = *(state_list[t]);
        for (int i=0; i<this->nbasins; i++) {
//            emiss[t*this->nbasins + i] = this->basins[i].P_state(this_state);
            if (state_list[t]) {
                emiss[t*this->nbasins + i] = (state_list[t]->P)[i];
            } else {
                emiss[t*this->nbasins + i] = 1;
            }
        }
    }
    return emiss;
}

template <class BasinT>
vector<double> HMM<BasinT>::get_forward() {
    return forward;
}

template <class BasinT>
vector<double> HMM<BasinT>::get_backward() {
    return backward;
}

template <class BasinT>
tuple <vector<double>, vector<double> > HMM<BasinT>::train(int niter) {
    
    cout << "Initializing EM params..." << endl;
    
    w0.assign(this->nbasins, 1/(double)this->nbasins);
    trans.assign(this->nbasins*this->nbasins, 1/(double)this->nbasins);
    this->basins.clear();
    // Initialize each basin model
    for (int i=0; i<this->nbasins; i++) {
        //        BasinT this_basin  BasinT(N));
        this->basins.push_back(BasinT(this->N,i,this->rng));
    }
    
    // Initialize emission probabilities
    for (state_iter it=this->train_states.begin(); it != this->train_states.end(); ++it) {
        State& this_state = it->second;
        for (int i=0; i<this->nbasins; i++) {
            this_state.P[i] = this->basins[i].P_state(this_state);
        }
    }
    cout << "forward" << endl;
    update_forward();
    cout << "backward" << endl;
    update_backward();
    cout << "P" << endl;
    update_P();

    
//    test_logli.assign(niter,0);
    vector<double> train_logli (niter);
    vector<double> test_logli (niter);
    for (int i=0; i<niter; i++) {
        cout << "Iteration " << i << endl;
        
        // E step

        for (int j=0; j<this->nbasins; j++) {
            this->basins[j].reset_stats();
        }
        
        for (state_iter it = this->train_states.begin(); it!=this->train_states.end(); ++it) {
            for (int j=0; j<this->nbasins; j++) {
                this->basins[j].increment_stats(it->second);
            }
        }
        for (int j=0; j<this->nbasins; j++) {
            this->basins[j].normalize_stats();
        }
        
        // M step

        //        double alpha = (i<niter/2) ? 1 - (double)i/(niter/2) : 0;
        double alpha = 0.002;
//        if (i >= niter/2) {
//            alpha = 0.002 + (1-0.002)*exp(-(double) (i-niter/2) * (10.0/(((double)(niter/2)-1))));
//        }
        //        cout << alpha << endl;
        for (int j=0; j<this->nbasins; j++) {
            
            this->basins[j].doMLE(alpha);
        }
        
        cout << "forward" << endl;
        update_forward();
        cout << "backward" << endl;
        update_backward();
        cout << "P" << endl;
        update_P();
        cout << "trans" << endl;
        update_trans();

        cout << "logli" <<endl;
        train_logli[i] = logli(true);
        test_logli[i] = logli(false);
        //test_logli[i] = update_P_test();
    }
    return make_tuple(train_logli, test_logli);

}


template <class BasinT>
void HMM<BasinT>::forward_backward() {
    
    // Forward pass
    vector<double> norm (T,0);
    for (int n=0; n<this->nbasins; n++) {
        backward[n] = w0[n];
    }
    norm[0] = 1;
    for (int t=1; t<T; t++) {
        //        State& this_state = this->train_states.at(words[t-1]);
//        double norm = 0;
        for (int n=0; n<this->nbasins; n++) {
            backward[this->nbasins*t + n] = 0;
            for (int m=0; m<this->nbasins; m++) {
                if (state_list[t-1]) {
                    backward[this->nbasins*t + n] += (state_list[t-1]->P)[m] * trans[m*this->nbasins+n] * backward[(t-1)*this->nbasins + m];
                } else {
                    backward[this->nbasins*t + n] += trans[m*this->nbasins+n] * backward[(t-1)*this->nbasins + m];
                    
                }
            }
            norm[t] += backward[this->nbasins*t + n];
        }
        for (int n=0; n<this->nbasins; n++) {
            backward[this->nbasins*t+n] /= norm[t];
        }
    }
    
    // Backward pass
    
    for (int n=0; n<this->nbasins; n++) {
        //        forward[(T-1)*this->nbasins+n] = final_state.P[n];
        if (state_list[T-1]) {
            forward[(T-1)*this->nbasins+n] = (state_list[T-1]->P)[n];
        } else {
            forward[(T-1)*this->nbasins+n] = 1;
        }
        norm += forward[(T-1)*this->nbasins+n];
    }
    
    for (int n=0; n<this->nbasins; n++) {
        forward[(T-1)*this->nbasins+n] /= norm;
    }
    
    
    for (int t=T-2; t>=0; t--) {
        //        State& this_state = this->train_states[words[t]];
        double norm = 0;
        for (int n=0; n<this->nbasins; n++) {
            //            forward[t*this->nbasins + n] = this_state.P[n];
            if (state_list[t]) {
                forward[t*this->nbasins + n] = (state_list[t]->P)[n];
            } else {
                forward[t*this->nbasins + n] = 1;
            }
            double tmp = 0;
            for (int m=0; m<this->nbasins; m++) {
                tmp += trans[n*this->nbasins + m] * forward[(t+1)*this->nbasins + m];
            }
            forward[t*this->nbasins + n] *= tmp;
            norm += forward[t*this->nbasins + n];
        }
        
        for (int n=0; n<this->nbasins; n++) {
            forward[t*this->nbasins + n] /= norm;
        }
        
    }


    return;
}

template <class BasinT>
vector<double> HMM<BasinT>::trans_at_t(int t) {
    return this->trans;
}

template <class BasinT>
void HMM<BasinT>::update_forward() {
//    State& final_state = this->train_states.at(words[T-1]);

    
    double norm = 0;
    int tmax = T + (T%tskip) - tskip;
    for (int n=0; n<this->nbasins; n++) {
//        forward[(T-1)*this->nbasins+n] = final_state.P[n];
        if (state_list[tmax]) {
            forward[tmax*this->nbasins+n] = (state_list[tmax]->P)[n];
        } else {
            forward[tmax*this->nbasins+n] = 1;
        }
        norm += forward[tmax*this->nbasins+n];
    }
    
    for (int n=0; n<this->nbasins; n++) {
        forward[tmax*this->nbasins+n] /= norm;
    }
    
    vector<double> this_trans (this->nbasins*this->nbasins);
    
    for (int t=tmax-tskip; t>=0; t-=tskip) {
//        State& this_state = this->train_states[words[t]];
        double norm = 0;
        this_trans = trans_at_t(t);
        
        for (int n=0; n<this->nbasins; n++) {
//            forward[t*this->nbasins + n] = this_state.P[n];
            if (state_list[t]) {
                forward[t*this->nbasins + n] = (state_list[t]->P)[n];
            } else {
                forward[t*this->nbasins + n] = 1;
            }
            double tmp = 0;
            for (int m=0; m<this->nbasins; m++) {
                tmp += this_trans[n*this->nbasins + m] * forward[(t+tskip)*this->nbasins + m];
            }
            forward[t*this->nbasins + n] *= tmp;            
            norm += forward[t*this->nbasins + n];
        }
        
        for (int n=0; n<this->nbasins; n++) {
            forward[t*this->nbasins + n] /= norm;
        }
         
    }
    return;
}


template <class BasinT>
void HMM<BasinT>::update_backward() {    
    for (int n=0; n<this->nbasins; n++) {
        backward[n] = w0[n];
    }
    vector<double> this_trans (this->nbasins*this->nbasins);
    for (int t=tskip; t<T; t+=tskip) {
//        State& this_state = this->train_states.at(words[t-1]);
        this_trans = trans_at_t(t);
        double norm = 0;
        for (int n=0; n<this->nbasins; n++) {
            backward[this->nbasins*t + n] = 0;
            for (int m=0; m<this->nbasins; m++) {
                if (state_list[t-tskip]) {
                    backward[this->nbasins*t + n] += (state_list[t-tskip]->P)[m] * this_trans[m*this->nbasins+n] * backward[(t-tskip)*this->nbasins + m];
                } else {
                    backward[this->nbasins*t + n] += this_trans[m*this->nbasins+n] * backward[(t-tskip)*this->nbasins + m];
                }
            }
            norm += backward[this->nbasins*t + n];
        }
        for (int n=0; n<this->nbasins; n++) {
            backward[this->nbasins*t+n] /= norm;
        }

    }
    return;
}

template <class BasinT>
void HMM<BasinT>::update_trans() {
    // Update w0
    double norm=0;
    for (int n=0; n<this->nbasins; n++) {
        w0[n] *= forward[n];
        norm += w0[n];
    }

    for (int n=0; n<this->nbasins; n++) {
        w0[n] /= norm;
    }
    
    // Update trans
    vector<double> num (this->nbasins*this->nbasins,0);
    vector<double> prob (this->nbasins*this->nbasins);
    for (int t=tskip; t<T; t+=tskip) {
//        State& this_state = this->train_states.at(words[t-1]);

        double norm = 0;
        for (int n=0; n<this->nbasins; n++) {
            double tmp;
            if (state_list[t-tskip]) {
                tmp = (state_list[t-tskip]->P)[n] * backward[(t-tskip)*this->nbasins+n];
            } else {
                tmp = backward[(t-tskip)*this->nbasins+n];
            }
            for (int m=0; m<this->nbasins; m++) {
                prob[n*this->nbasins+m] = tmp * trans[n*this->nbasins + m] * forward[t*this->nbasins + m];
                norm += prob[n*this->nbasins + m];
            }
        }
        for (int n=0; n<this->nbasins*this->nbasins; n++) {
            prob[n] /= norm;
        }
        for (int n=0; n<this->nbasins; n++) {
            for (int m=0; m<this->nbasins; m++) {
                double delta_num = prob[n*this->nbasins+m] - num[n*this->nbasins+m];
                num[n*this->nbasins+m] += delta_num / t;
            }
        }
    }
    for (int n=0; n<this->nbasins; n++) {
        double norm = 0;
        for (int m=0; m<this->nbasins; m++) {
            norm += num[n*this->nbasins+m];
        }
        for (int m=0; m<this->nbasins; m++) {
            trans[n*this->nbasins+m] = num[n*this->nbasins+m]/norm;
        }
    }
    return;
}

template <class BasinT>
void HMM<BasinT>::update_P() {

    for (state_iter it=this->train_states.begin(); it != this->train_states.end(); ++it) {
        State& this_state = it->second;
        this_state.weight.assign(this->nbasins,0);
    }
    
    vector<double> denom  (this->nbasins,0);
    int nsamp = 0;
    vector<double>  this_P (this->nbasins,0);
    for (int t=0; t<T; t+=tskip) {
//        State& this_state = this->train_states.at(words[t]);
        if (state_list[t]) {
            State& this_state = *state_list[t];
            
            double norm = 0;
            for (int i=0; i<this->nbasins; i++) {
                this_P[i] = forward[t*this->nbasins + i] * backward[t*this->nbasins + i];
                norm += this_P[i];
            }
            for (int i=0; i<this->nbasins; i++) {
                this_P[i] /= norm;
                
                //double delta = this_P[i] - this_state.weight[i];
               // this_state.weight[i] += delta / (i+1);
                this_state.weight[i] += this_P[i];
                //denom[i] += this_P[i];
//                denom[i] += (delta_denom / ((t/tskip)+1));
                double delta_denom = this_P[i] - denom[i];
                denom[i] += (delta_denom / (nsamp+1));
            }
            nsamp++;
        }
    }
    
    for (state_iter it=this->train_states.begin(); it != this->train_states.end(); ++it) {
        State& this_state = it->second;
        for (int i=0; i<this->nbasins; i++) {
//            this_state.weight[i] /= (ceil(T/tskip)*denom[i]);
            this_state.weight[i] /= (nsamp*denom[i]);
//            this_state.weight[i] /= (denom[i]);
            this_state.P[i] = this->basins[i].P_state(this_state);
        }
    }

    return;
}

template <class BasinT>
double HMM<BasinT>::logli(bool obs) {
    vector<int> alpha = viterbi(obs);
//    State& init_state = this->train_states.at(words[0]);
    vector<double> emiss = emiss_obs(obs, tskip-1);
    // don't use epsilon ~ 10^-16, use min ~ 10^-308
    // see https://en.cppreference.com/w/cpp/types/numeric_limits
    //modified line
    //double logmin = log( std::numeric_limits<double>::min() );
    double logmin =  std::numeric_limits<double>::min();
    //cout << "logmin" << logmin;
    double logli = log(w0[alpha[tskip-1]] * emiss[alpha[tskip-1]]);
    // Aditya note: w0 ~= 0 or emiss ~= 0 causes -inf, thence nan's,
    //  so lower bound to min representable positive number
    if (std::isinf(logli)) {
        logli = logmin;
    }
    for (int t=2*tskip-1; t<T; t+=tskip) {
//        State& this_state = this->train_states.at(words[t]);
        emiss = emiss_obs(obs, t);
        // Aditya notes: why subtract -logli at each time step!?
        // Basically, they're doing an online i.e. running mean as each data point arrives (useful if tskip > 1).
        // At time step t, suppose mean was correct, i.e. already divided by t,
        //  then at time step t+1, you want the previous mean to be multiplied by t/(t+1) to get an overall /(t+1).
        // mean_0 = val_0
        // mean_{t+1} = val_{t+1} /(t+1) + mean_t * t/(t+1) 
        //            =  val_{t+1} /(t+1) + mean_t (1 - 1/(t+1))
        //            = ( val_{t+1} - mean_t )/(t+1) + mean_t
        //            = delta_{t+1} /(t+1) + mean_t,     where delta_{t+1} = val_{t+1} - mean_t
        double logemiss = log(emiss[alpha[t]]);
        // Aditya note: emiss or trans ~= 0 causes -inf, thence nan's,
        //  so lower bound to min representable positive number
        if (std::isinf(logemiss)) {
            logemiss = logmin;
        }
        double logtrans = log(trans[alpha[t-tskip]*this->nbasins + alpha[t]]);
        if (std::isinf(logtrans)) {
            logtrans = logmin;
        }
        double delta = logtrans + logemiss - logli;
        // Aditya notes: for non-running mean, below RHS is val_{t+1} in explanation above
        //double delta = log(trans[alpha[t-tskip]*this->nbasins + alpha[t]]) + log(emiss[alpha[t]]);

        logli += delta / (((t-1)/tskip)+1);
        // logli += delta;  // Aditya notes: for non-running mean (assumed tskip=1)
    }
    return logli;
    //return logli/T;       // Aditya notes: normalize at end for non-running mean
    
//    State& final_state = this->train_states.at(words[T-1]);
//    vector<double> logli (this->nbasins, 0);
//    for (int n=0; n<this->nbasins; n++) {
//        logli[n] = log(final_state.P[n]);
//    }
//
//    for (int t=T-2; t>=0; t--) {
//        vector<double> tmp_logli = logli;
//        State& this_state = this->train_states[words[t]];
//        for (int n=0; n<this->nbasins; n++) {            
//            double tmp = 0;
//            for (int m=0; m<this->nbasins; m++) {
//                tmp += trans[n*this->nbasins + m] * exp(tmp_logli[m]);
//            }
//            logli[n] = log(this_state.P[n]) + log(tmp);
//        }        
//    }
//
//    double li = 0;
//    for (int n=0; n<this->nbasins; n++) {
//        li += exp(logli[n])*w0[n];
//    }
//    return log(li);
}

/*
template <class BasinT>
vector<char> HMM<BasinT>::get_raster() {
    
    vector<char> raster (T*this->N, 0);
    for (int t=0; t<T; t++) {
        string this_str = words[t];
        for (int n=0; n<this->N; n++) {
            raster[t*this->N + n] = this_str[n];
        }
    }
    return raster;
}
*/

template <class BasinT>
vector<double> HMM<BasinT>::get_P() {
    vector<double> P (T*this->nbasins);
    for (int t=0; t<T; t++) {
        double norm = 0;
        for (int n=0; n<this->nbasins; n++) {
            P[t*this->nbasins+n] = forward[t*this->nbasins+n]*backward[t*this->nbasins+n];
            norm += P[t*this->nbasins+n];
        }
        for (int n=0; n<this->nbasins; n++) {
            P[t*this->nbasins+n] /= norm;
        }
    }
    return P;
}

template <class BasinT>
vector<double> HMM<BasinT>::P_indep() {
    vector<double> P (T*this->nbasins);
    for (int t=0; t<T; t++) {
        double norm = 0;        
        for (int n=0; n<this->nbasins; n++) {
            P[t*this->nbasins+n] = this->w[n] * state_list[t]->P[n];
            norm += P[t*this->nbasins+n];
        }
        for (int n=0; n<this->nbasins; n++) {
            P[t*this->nbasins+n] /= norm;
        }

    }
    return P;
}

template <class BasinT>
vector<double> HMM<BasinT>::get_trans() {
    return trans;
}

template<class BasinT>
vector<double> HMM<BasinT>::emiss_obs(bool obs, int t) {
   
 if (state_list[t] && obs) {
        return state_list[t]->P;
	} else if (!state_list[t] && !obs) {
        vector<double> emiss (this->nbasins, 1);
        State this_state;
        this_state.word.assign(this->N, 0);
        for (int n=0; n<this->N; n++) {
            if ((this->raster[t])[n] == '1') {
                this_state.on_neurons.push_back(n);
                this_state.word[n] = 1;
            }
        }
        for (int k=0; k<this->nbasins; k++) {
            emiss[k] = (this->basins)[k].P_state(this_state);
        }
        return emiss;
    }
    return vector<double> (this->nbasins,1);

}

template <class BasinT>
vector<int> HMM<BasinT>::viterbi(bool obs) {
    vector<int> alpha_max (T,0);
    vector<int> argmax (T*this->nbasins, 0);

    vector<double> max (this->nbasins, 1);
    vector<double> emiss (this->nbasins);
    for (int t=(T-(T%tskip)-1); t>=(tskip-1); t-=tskip) {
//        State& this_state = this->train_states.at(words[t]);
        emiss = emiss_obs(obs,t);
        double norm = 0;
        for (int n=0; n<this->nbasins; n++) {
            double this_max = 0;
            int this_arg = 0;
            for (int m=0; m<this->nbasins; m++) {
                double tmp = emiss[m] * trans[n*this->nbasins+m] * max[m];
//                if (state_list[t]) {
//                    tmp = (state_list[t]->P)[m] * trans[n*this->nbasins+m] * max[m];
//                } else {
//                    tmp = trans[n*this->nbasins+m] * max[m];
//                }
                
                if (tmp > this_max) {
                    this_max = tmp;
                    this_arg = m;
                }
            }
            max[n] = this_max;
            norm += this_max;
            argmax[t*this->nbasins + n] = this_arg;
        }
        for (int n=0; n<this->nbasins; n++) {
            max[n] /= norm;
        }
    }
//    State& this_state = this->train_states.at(words[0]);
    double this_max = 0;
    int this_arg = 0;
    emiss = emiss_obs(obs,tskip-1);
    for (int m=0; m<this->nbasins; m++) {
        double tmp = emiss[m] * w0[m] * max[m];
        if (tmp > this_max) {
            this_max = tmp;
            this_arg = m;
        }
    }
    alpha_max[tskip-1] = this_arg;
    for (int t=2*tskip-1; t<T; t+=tskip) {
        alpha_max[t] = argmax[t*this->nbasins + alpha_max[t-tskip]];
    }
    
    return alpha_max;
}

template <class BasinT>
vector<double> HMM<BasinT>::stationary_prob() {
    // Stationary basin probability
    vector<double> trans_pow = mpow(trans, this->nbasins, 1000);
    vector<double> w (this->nbasins,0);
    for (int i=0; i<this->nbasins; i++) {
        for (int j=0; j<this->nbasins; j++) {
            w[i] += trans_pow[(this->nbasins)*j+i]*w0[j];
        }
    }
    return w;
}


template <class BasinT>
vector<char> HMM<BasinT>::sample(int nsamples) {
    vector<char> samples (this->N*nsamples);
    int basin_ind = (this->rng)->discrete(w0);
    vector<char> this_sample = (this->basins)[basin_ind].sample();
    for (int n=0; n<this->N; n++) {
        samples[n] = this_sample[n];
    }
    for (int t=1; t<nsamples; t++) {
        vector<double> this_trans (this->nbasins);
        for (int k=0; k<this->nbasins; k++) {
            this_trans[k] = trans[basin_ind*this->nbasins + k];
        }
        basin_ind = (this->rng)->discrete(this_trans);
        this_sample = (this->basins)[basin_ind].sample();
        // Aditya notes: Careful, the samples "matrix" (nNeurons x nTimeBins)
        //  is represented as a vector here and is filled in column-major format below
        //  whereas C++ default is row-major and that's what I've assumed in writePyOutputMatrix
        //  so after passing to python, be sure to convert to row-major, else fiasco!
        for (int n=0; n<this->N; n++) {
            samples[t*this->N + n] = this_sample[n];
        }
    }
    return samples;
}

template <class BasinT>
pair<vector<double>, vector<double> > HMM<BasinT>::pred_prob() {
    
    this->test_states.clear();
    for (int t=2*tskip-1; t<T; t+=tskip) {
        if (state_list[t]) {
            State this_state = *(state_list[t]);
            this_state.freq = 0;
            vector<char> this_word = this_state.word;
            string this_str (this_word.size(), '0');
            for (int i=0; i<this_word.size(); i++) {
                this_str[i] = this_word[i];
            }
            pair<map<string, State>::iterator, bool> ins = this->test_states.insert(pair<string,State> (this_str, this_state));
            State& inserted_state = (ins.first)->second;
            inserted_state.freq++;
        }
    }
    
    vector<double> w = stationary_prob();
    vector<double> prob (this->test_states.size(), 0);
    vector<double> freq (this->test_states.size(), 0);
    int ix = 0;

    for (map<string, State>::iterator it = (this->test_states).begin(); it != (this->test_states).end(); ++it) {
        for (int i=0; i<this->nbasins; i++) {
            double this_P = this->basins[i].P_state(it->second);
            prob[ix] += w[i] * this_P;
            freq[ix] = (it->second).freq;
        }
        ix++;
    }

    return pair<vector<double>, vector<double> > (prob, freq);
    
    
}


template <class BasinT>
Autocorr<BasinT>::Autocorr(vector<vector<double> >& st, double binsize, int nbasins) : HMM<BasinT>(st,vector<double>(),vector<double>(),binsize,nbasins), basin_trans (nbasins * st.size()) {

    for (vector<double*>::iterator it = basin_trans.begin(); it != basin_trans.end(); ++it) {
        *it = new double[4];
    }
}


template <class BasinT>
Autocorr<BasinT>::~Autocorr() {
    for (vector<double*>::iterator it = basin_trans.begin(); it != basin_trans.end(); ++it) {
        delete[] *it;
    }
}


template <class BasinT>
void Autocorr<BasinT>::update_basin_trans_indep() {
    vector<vector<double> > basin_trans_num (this->nbasins * this->N, vector<double> (4,0));
    vector<vector<double> > basin_trans_den (this->nbasins * this->N, vector<double> (2,0));
    vector<double> denom  (this->nbasins,0);
    
    for (int t=1; t<this->T; t++) {
        vector<double> P_joint(this->nbasins * this->nbasins, 0);
        double norm = 0;
        for (int n=0; n<this->nbasins; n++) {
            for (int m=0; m<this->nbasins; m++) {
                P_joint[n*this->nbasins+m] = this->state_list[t]->P[m] * this->state_list[t-1]->P[n] * this->w[n] * this->w[m];
                norm += P_joint[n*this->nbasins + m];
            }
        }
        for (int n=0; n<this->nbasins*this->nbasins; n++) {
            P_joint[n] /= norm;
        }
        for (int i=0; i<this->nbasins; i++) {
            for (int n=0; n<this->N; n++) {
                char sp_prev = (this->state_list[t-1]->word)[n];
                char sp_this = (this->state_list[t]->word)[n];

//                for (int a=0; a<4; a++) {
//                    double delta_num = -basin_trans_num[this->N*i+n][a];
//                    delta_num += (a == (sp_this + 2*sp_prev)) ? P_joint[i*this->nbasins+i] : 0;
//                    basin_trans_num[this->N*i + n][a] += delta_num / (t+1);
//                }
//
//                for (int a=0; a<2; a++) {
//                    double delta_den = -basin_trans_den[this->N*i+n][a];
//                    delta_den += (a == sp_prev) ? P_joint[i*this->nbasins+i] : 0;
//                    basin_trans_den[this->N*i + n][a] += delta_den / (t+1);
//                }
                
                basin_trans_num[this->N*i + n][sp_this + 2*sp_prev] += P_joint[i*this->nbasins+i];
                basin_trans_den[this->N*i + n][sp_prev] += P_joint[i*this->nbasins+i];
            }
        }
        
    }
    
    for (int i=0; i<this->nbasins * this->N; i++) {
        if (basin_trans_den[i][0] > 0) {
            basin_trans[i][0] = basin_trans_num[i][0] / basin_trans_den[i][0];
            // basin_trans[i][1] = 1 - basin_trans[i][0];
            basin_trans[i][1] = basin_trans_num[i][1] / basin_trans_den[i][0];
        } else {
            basin_trans[i][0] = 0.5;
            basin_trans[i][1] = 0.5;
        }
        if (basin_trans_den[i][1] > 0) {
            basin_trans[i][2] = basin_trans_num[i][2] / basin_trans_den[i][1];
            basin_trans[i][3] = basin_trans_num[i][3] / basin_trans_den[i][1];
        } else {
            basin_trans[i][2] = 0.5;
            basin_trans[i][3] = 0.5;
   
        }
        // basin_trans[i][3] = 1 - basin_trans[i][2];
    }

    
    return;
}



template <class BasinT>
void Autocorr<BasinT>::update_P() {
    
    for (state_iter it=this->train_states.begin(); it != this->train_states.end(); ++it) {
        State& this_state = it->second;
        this_state.weight.assign(this->nbasins,0);
    }
    
    vector<vector<double> > basin_trans_num (this->nbasins * this->N, vector<double> (4,0));
    vector<vector<double> > basin_trans_den (this->nbasins * this->N, vector<double> (2,0));
    vector<double> denom  (this->nbasins,0);
    
    for (int t=1; t<this->T; t++) {
        //        State& this_state = this->train_states.at(words[t]);
        State& this_state = *(this->state_list[t]);
        
        vector<double> this_P  (this->nbasins,0);
        vector<double> this_trans = trans_at_t(t);

        vector<double> P_joint(this->nbasins * this->nbasins, 0);
        double norm = 0;
        for (int n=0; n<this->nbasins; n++) {
            for (int m=0; m<this->nbasins; m++) {
                P_joint[n*this->nbasins+m] = this->backward[t*this->nbasins + m] * this_trans[n*this->nbasins + m] * this->forward[(t-1)*this->nbasins + n];
                norm += P_joint[n*this->nbasins + m];
            }
        }
        for (int n=0; n<this->nbasins*this->nbasins; n++) {
            P_joint[n] /= norm;
        }
        

        for (int i=0; i<this->nbasins; i++) {
            for (int j=0; j<this->nbasins; j++) {
                if (j != i) {
                    this_P[i] += P_joint[j*this->nbasins + i];
                }
            }
        }
        
        for (int i=0; i<this->nbasins; i++) {
            
            double delta_denom = this_P[i] - denom[i];

            this_state.weight[i] += this_P[i];

            denom[i] += (delta_denom / (t+1));
            
            for (int n=0; n<this->N; n++) {
                char sp_prev = (this->state_list[t-1]->word)[n];
                char sp_this = this_state.word[n];
                
                for (int a=0; a<4; a++) {
                    double delta_num = -basin_trans_num[this->N*i+n][a];
                    delta_num += (a == (sp_this + 2*sp_prev)) ? P_joint[i*this->nbasins+i] : 0;
                    basin_trans_num[this->N*i + n][a] += delta_num / (t+1);
                }
                
                for (int a=0; a<2; a++) {
                    double delta_den = -basin_trans_den[this->N*i+n][a];
                    delta_den += (a == sp_prev) ? P_joint[i*this->nbasins+i] : 0;
                    basin_trans_den[this->N*i + n][a] += delta_den / (t+1);
                }
//                basin_trans_num[this->N*i + n][sp_this + 2*sp_prev] += P_joint[i*this->nbasins+i];
//                basin_trans_den[this->N*i + n][sp_prev] += P_joint[i*this->nbasins+i];
            }
        }
    }
    
    for (int i=0; i<this->nbasins * this->N; i++) {
        if (basin_trans_den[i][0] > 0) {
            basin_trans[i][0] = basin_trans_num[i][0] / basin_trans_den[i][0];
            // basin_trans[i][1] = 1 - basin_trans[i][0];
            basin_trans[i][1] = basin_trans_num[i][1] / basin_trans_den[i][0];
        } else {
            basin_trans[i][0] = 0.5;
            basin_trans[i][1] = 0.5;
        }
        if (basin_trans_den[i][1] > 0) {
            basin_trans[i][2] = basin_trans_num[i][2] / basin_trans_den[i][1];
            basin_trans[i][3] = basin_trans_num[i][3] / basin_trans_den[i][1];
        } else {
            basin_trans[i][2] = 0.5;
            basin_trans[i][3] = 0.5;
            
        }
        // basin_trans[i][3] = 1 - basin_trans[i][2];
    }
    
    for (state_iter it=this->train_states.begin(); it != this->train_states.end(); ++it) {
        State& this_state = it->second;
        for (int i=0; i<this->nbasins; i++) {
            this_state.weight[i] /= (this->T * denom[i]);
            //            this_state.weight[i] /= (denom[i]);
            this_state.P[i] = this->basins[i].P_state(this_state);
        }
    }
    
    return;
}

template <class BasinT>
void Autocorr<BasinT>::update_w() {
    vector<double> next_w (this->nbasins,0);
    for (int t=0; t<this->T; t++) {
        for (int i=0; i<this->nbasins; i++) {
            double delta_w = this->forward[t*this->nbasins + i] * this->backward[t*this->nbasins + i] - next_w[i];
            next_w[i] += delta_w / (t+1);
        }
    }
    double norm = 0;
    for (int i=0; i<this->nbasins; i++) {
        norm += next_w[i];
    }
    for (int i=0; i<this->nbasins; i++) {
        next_w[i] /= norm;
    }
    this->w = next_w;
    return;
}


template <class BasinT>
vector<double> Autocorr<BasinT>::trans_at_t(int t) {
    vector<double> trans (this->nbasins * this->nbasins, 0);
    for (int a=0; a<this->nbasins; a++) {
        for (int b=0; b<this->nbasins; b++) {
            trans[this->nbasins*a + b] = this->w[b];
            if (a==b) {
                for (int n=0; n<this->N; n++) {
                    char sp_prev = this->state_list[t-1]->word[n];
                    char sp_this = this->state_list[t]->word[n];
                    trans[this->nbasins*a + b] *= basin_trans[this->N*a + n][sp_this + 2*sp_prev];
                }
            } else {
                trans[this->nbasins*a + b] *= this->state_list[t]->P[b];
            }
        }
    }
    return trans;
}

template <class BasinT>
void Autocorr<BasinT>::update_forward() {
    for (int n=0; n<this->nbasins; n++) {
        this->forward[n] = this->w[n] * (this->state_list[0]->P)[n];
    }
    for (int t=1; t<this->T; t++) {
        //        State& this_state = this->train_states.at(words[t-1]);
        vector<double> this_trans = trans_at_t(t);
        double norm = 0;
        for (int n=0; n<this->nbasins; n++) {
            this->forward[this->nbasins*t + n] = 0;
            for (int m=0; m<this->nbasins; m++) {
                this->forward[this->nbasins*t + n] += this_trans[m*this->nbasins+n] * this->forward[(t-1)*this->nbasins + m];
            }
            norm += this->forward[this->nbasins*t + n];
        }
        for (int n=0; n<this->nbasins; n++) {
            this->forward[this->nbasins*t+n] /= norm;
        }
        
    }
    return;

    
}

template <class BasinT>
void Autocorr<BasinT>::update_backward() {

    int tmax = this->T - 1;
    for (int n=0; n<this->nbasins; n++) {
        //        forward[(T-1)*this->nbasins+n] = final_state.P[n];
        this->backward[tmax*this->nbasins+n] = 1;
    }
    
    for (int t=tmax-1; t>=0; t--) {
        //        State& this_state = this->train_states[words[t]];
        double norm = 0;
        vector<double> this_trans = trans_at_t(t+1);
        
        for (int n=0; n<this->nbasins; n++) {
            //            forward[t*this->nbasins + n] = this_state.P[n];
            this->backward[t*this->nbasins + n] = 0;
            
            for (int m=0; m<this->nbasins; m++) {
                this->backward[t*this->nbasins + n] += this_trans[n*this->nbasins + m] * this->backward[(t+1)*this->nbasins + m];
            }
            norm += this->backward[t*this->nbasins + n];
        }
        
        for (int n=0; n<this->nbasins; n++) {
            this->backward[t*this->nbasins + n] /= norm;
        }
        
    }
    return;
}

template <class BasinT>
vector<double> Autocorr<BasinT>::train(int niter) {
    /*
    (this->w).assign(this->nbasins, 1/(double)this->nbasins);
    this->basins.clear();
    // Initialize each basin model
    for (int i=0; i<this->nbasins; i++) {
        this->basins.push_back(BasinT(this->N,i,this->rng));
    }
    
    // Initialize emission probabilities
    for (state_iter it=this->train_states.begin(); it != this->train_states.end(); ++it) {
        State& this_state = it->second;
        for (int i=0; i<this->nbasins; i++) {
            this_state.P[i] = this->basins[i].P_state(this_state);
        }
    }
    
    for (int i=0; i<this->nbasins * this->N; i++) {
//        basin_trans[i][0] = 0.1*((double) rand() / (double) RAND_MAX) + 0.45;
        basin_trans[i][0] = 0.5;
        basin_trans[i][1] = 1 - basin_trans[i][0];
//        basin_trans[i][2] = 0.1*((double) rand() / (double) RAND_MAX) + 0.45;
        basin_trans[i][2] = 0.5;
        basin_trans[i][3] = 1 - basin_trans[i][2];
    }
     */
    int uncorr_iter = 20;
    uncorr_iter = (uncorr_iter < niter) ? uncorr_iter : niter;
//    vector<double> train_logli_begin = this->EMBasins<BasinT>::train(uncorr_iter);
    vector<double> train_logli_begin = this->HMM<BasinT>::train(uncorr_iter);
//    for (int i=0; i<this->nbasins * this->N; i++) {
//     //        basin_trans[i][0] = 0.1*((double) rand() / (double) RAND_MAX) + 0.45;
//         basin_trans[i][0] = 0.5;
//         basin_trans[i][1] = 1 - basin_trans[i][0];
//         //        basin_trans[i][2] = 0.1*((double) rand() / (double) RAND_MAX) + 0.45;
//         basin_trans[i][2] = 0.5;
//         basin_trans[i][3] = 1 - basin_trans[i][2];
//    }

    //this->EMBasins<BasinT>::update_P();
    
    update_basin_trans_indep();
//    
    update_forward();
    update_backward();
    update_P();
    update_w();
    
    vector<double> train_logli (niter);
    for (int i=0; i<uncorr_iter; i++) {
        train_logli[i] = train_logli_begin[i];
    }
    for (int i=uncorr_iter; i<niter; i++) {
        cout << "Iteration " << i << endl;
        
        // E step
        
        for (int j=0; j<this->nbasins; j++) {
            this->basins[j].reset_stats();
        }
        
        for (state_iter it = this->train_states.begin(); it!=this->train_states.end(); ++it) {
            for (int j=0; j<this->nbasins; j++) {
                this->basins[j].increment_stats(it->second);
            }
        }
        for (int j=0; j<this->nbasins; j++) {
            this->basins[j].normalize_stats();
        }
        
        // M step
        
        //        double alpha = (i<niter/2) ? 1 - (double)i/(niter/2) : 0;
        double alpha = 0.002;
        //        if (i >= niter/2) {
        //            alpha = 0.002 + (1-0.002)*exp(-(double) (i-niter/2) * (10.0/(((double)(niter/2)-1))));
        //        }
        //        cout << alpha << endl;
        for (int j=0; j<this->nbasins; j++) {
            
            this->basins[j].doMLE(alpha);
        }

        cout << "Forward..." << endl;
        update_forward();
        cout << "Backward..." << endl;
        update_backward();
        cout << "P..." << endl;
        update_P();
        update_w();
        cout << "logli..." << endl;
        train_logli[i] = logli();
    }
    
    

    return train_logli;
    
}


template <class BasinT>
vector<int> Autocorr<BasinT>::viterbi() {
    
    vector<int> alpha_max (this->T,0);
    vector<int> argmax (this->T*this->nbasins, 0);
    
    vector<double> max (this->nbasins, 1);
    for (int t=(this->T-1); t>0; t--) {
        double norm = 0;
        vector<double> this_trans = trans_at_t(t);
        for (int n=0; n<this->nbasins; n++) {
            double this_max = 0;
            int this_arg = 0;
            for (int m=0; m<this->nbasins; m++) {
                double tmp = this_trans[n*this->nbasins+m] * max[m];
                
                if (tmp > this_max) {
                    this_max = tmp;
                    this_arg = m;
                }
            }
            max[n] = this_max;
            norm += this_max;
            argmax[t*this->nbasins + n] = this_arg;
        }
        for (int n=0; n<this->nbasins; n++) {
            max[n] /= norm;
        }
    }
    //    State& this_state = this->train_states.at(words[0]);
    double this_max = 0;
    int this_arg = 0;
    for (int m=0; m<this->nbasins; m++) {
        double tmp = this->w[m] * (this->state_list[0]->P)[m] * max[m];
        if (tmp > this_max) {
            this_max = tmp;
            this_arg = m;
        }
    }
    alpha_max[0] = this_arg;
    for (int t=1; t<this->T; t++) {
        alpha_max[t] = argmax[t*this->nbasins + alpha_max[t-1]];
    }
    
    return alpha_max;
}

template <class BasinT>
double Autocorr<BasinT>::logli() {
    
    vector<int> alpha = viterbi();
    //    State& init_state = this->train_states.at(words[0]);
    cout << "Viterbi done." << endl;
    
    double logli = log(this->w[alpha[0]] * (this->state_list[0]->P)[alpha[0]]);
    
    
    for (int t=1; t<this->T; t++) {
        //        State& this_state = this->train_states.at(words[t]);
        vector<double> this_trans = trans_at_t(t);
        double delta = log(this_trans[alpha[t-1]*this->nbasins + alpha[t]]) - logli;
        
        logli += delta / t;
    }
    return logli;
}

template <class BasinT>
vector<double> Autocorr<BasinT>::get_basin_trans() {
    
    vector<double> basin_trans_out (4 * this->nbasins * this->N);
    for (int n=0; n<this->N; n++) {
        for (int a=0; a<this->nbasins; a++) {
            for (int i=0; i<4; i++) {
                basin_trans_out[4*this->N*a + 4*n + i] = basin_trans[this->N*a + n][i];
            }
        }
    }
    return basin_trans_out;
}
