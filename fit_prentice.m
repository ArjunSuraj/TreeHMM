close all;
clear all;

loaddir = "../UnsupervisedLearningNeuralData/Learnability_data/"
filename = "IST-2017-61-v1+1_bint_fishmovie32_100";
%loaddir = "~/Documents/Projects/MaximumEntropy/Olivier/fishmovie32/"
%filename = "bint_fishmovie32_100";
retinaData = load(loaddir+filename+".mat",'bint');

%binsize = 200; % binsize is # of samples / 20 ms time bin @ 10 kHz sampling rate.
binsize = 1;
nbasins = 70;
niter = 100;

spikeRaster = retinaData.bint;

% compile with #define matlabHMM in EMBasins.cpp and set below matlabHMM = 1
% if commented there, then model doesn't use temporal correlations, set matlabHMM = 0
matlabHMM = 1

if matlabHMM == 0 % without temporal correlations
    testIdx = 1:10:297;
    trainIdx = setdiff(1:297, testIdx);

    spikeRasterTrain = spikeRaster(trainIdx, :, :);
    spikeRasterTest = spikeRaster(testIdx, :, :);

    [numtrials,numneurons,numbins] = size(spikeRasterTrain);
    bins = 1:numbins*numtrials;
    nrnSpikeTimes = cell(numneurons,1);

    for nrnnum = 1:numneurons
        for trialnum = 1:numtrials
            spikeTimes = find(spikeRasterTrain(trialnum,nrnnum,:)) + (trialnum-1)*numbins;
            nrnSpikeTimes{nrnnum} = [nrnSpikeTimes{nrnnum},spikeTimes'];
        end
    end

    [numtrials,numneurons,numbins] = size(spikeRasterTest);
    bins = 1:numbins*numtrials;
    nrnSpikeTimesTest = cell(numneurons,1);

    for nrnnum = 1:numneurons
        for trialnum = 1:numtrials
            spikeTimes = find(spikeRasterTest(trialnum,nrnnum,:)) + (trialnum-1)*numbins;
            nrnSpikeTimesTest{nrnnum} = [nrnSpikeTimesTest{nrnnum},spikeTimes'];
        end
    end

    % validation/test set is statistically very different from training set --
    %  I got poor test log-likelihood despite high training log-likelihood
    % maybe because similar images were presented temporally together during the experiment?
    % to overcome this, I'm randomly permuting the full dataset
    % achtung: problematic if fitting a temporal model and/or retina has adaptation
    %shuffled_idxs = np.random.permutation(np.arange(tSteps,dtype=np.int32))
    %spikeRaster = spikeRaster[:,shuffled_idxs]

    [Pw, params, samples, P_emp, condP, P_model, LL_train, LL_test] = ...
        EMBasins(nrnSpikeTimes, nrnSpikeTimesTest, binsize, nbasins, niter);

else % with temporal correlations

    %% -- Initilize Specifications --
    Nc            = 2;            %Defines Nc-fold cross-validation (for HMM only)

    [numtrials,numneurons,numbins] = size(spikeRaster);
    bins = 1:numbins*numtrials;
    nrnSpikeTimes = cell(numneurons,1);

    for nrnnum = 1:numneurons
        for trialnum = 1:numtrials
            spikeTimes = find(spikeRaster(trialnum,nrnnum,:)) + (trialnum-1)*numbins;
            nrnSpikeTimes{nrnnum} = [nrnSpikeTimes{nrnnum},spikeTimes'];
        end
    end

    goodcells = 1:length(nrnSpikeTimes); 

    if Nc>1 %HMM syntax w/ Cross-Val
        % -- Compute Time Bin Indices for Nc-Fold Cross-Validation: -- 
        tmax = max(cell2mat(cellfun(@(x) max(double(x)), nrnSpikeTimes, ...
                   'UniformOutput', 0))); %tmax is in units of 10 kHz

        bins         = 0:binsize:tmax;
        s            = RandStream('mt19937ar','Seed',0);
        shuffle_bins = randperm(s,length(bins));
        ntest        = floor(length(bins)/Nc);

        %- Computations: -- 
        for k = 1:Nc

        testbins   = shuffle_bins((k-1)*ntest+1:k*ntest);
        train_bins = zeros(1,length(bins));
        train_bins(testbins) = 1;

        unobserved_low = bins(diff([0,train_bins]) == 1);
        unobserved_hi  = bins(diff([0,train_bins]) == -1);
        if (length(unobserved_hi) < length(unobserved_low))
            unobserved_hi = [unobserved_hi, tmax]; %#ok
        end

        [params,trans,P,emiss_prob,alpha,pred_prob,hist,sample,...
            stationary_prob,train_logli(:,k),test_logli(:,k)] = ...
                EMBasins(nrnSpikeTimes(goodcells), [unobserved_low', unobserved_hi'], ...
                    binsize, nbasins, niter); %#ok
        end %for
        
        % -- Save: --
        Output.train_logli= train_logli;
        Output.test_logli = test_logli;
        Output.trans      = trans;
        Output.P          = P;
        Output.emiss_prob = emiss_prob;
        Output.alpha      = alpha;
        Output.pred_prob  = pred_prob;
        Output.hist       = hist;
        Output.params     = params;
        Output.sample     = sample;
        Output.stationary_prob = stationary_prob;
        savename = "HMM_Params"+"_nb"+num2str(nbasins)+"_"+num2str(Nc)+"cv.mat";
        save(loaddir+savename,'Output');
        
    elseif Nc==1 %Use ALL data to determine params
        
        [params,trans,P,emiss_prob,alpha,pred_prob,hist,sample,...
            stationary_prob,train_logli,test_logli] = ...
                EMBasins(nrnSpikeTimes(goodcells), [], binsize, nbasins, niter); 
        
        % -- Save: --
        Output.train_logli= train_logli;
        Output.test_logli = test_logli;
        Output.trans      = trans;
        Output.P          = P;
        Output.emiss_prob = emiss_prob;
        Output.alpha      = alpha;
        Output.pred_prob  = pred_prob;
        Output.hist       = hist;
        Output.params     = params;
        Output.sample     = sample;
        Output.stationary_prob = stationary_prob;

        savename = "HMM_Params"+"_nb"+num2str(nbasins)+".mat";
        save(loaddir+savename,'Output');
    end

end
