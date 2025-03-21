% FLASH_COMPLEMENTARY_SIM 
% Simulate a flash image with complementary codes at many focal zones
% VERSION 1.0, August 10, 2016 (Tarek)
%
% This version uses BFT to beamform images
% 
% Version 1.1, August 11, 2016 (David)
% Adds calcuation of axial and lateral full width half maximum
% as well as signal to noise ratio
%
% Uses walking aperture, looping over codes
% -Wings are not great
% No support for signal to noise ratio yet
%% Initial setup (ex. transducer, phantom, image range properties) 
clear

% Define codes used for excitation
codes = cell(0);
codes{1}.code = [ones(1, 8), ones(1, 8), ones(1, 8), -ones(1, 8)];
codes{1}.ccode = [ones(1, 8), ones(1, 8), -ones(1, 8), ones(1, 8)];

% tempDat = load('bestPairsSQP41.mat');
% x = tempDat.('x');
% codes{1}.code = x(1,:); 
% codes{1}.ccode = x(2,:);

codes{1}.focus = [0 0 50/1000]; 

% Transducer properties
f0 = 5e6;              %  Central frequency                        [Hz]
fs = 100e6;            %  Sampling frequency                       [Hz]
c = 1540;              %  Speed of sound                           [m/s]
no_elements = 192;     %  Number of elements in the transducer     
no_active_tx = 64;      %  Number of transmitting elements in walking aperture
no_active_rx = 64;      %  Number of receiving elements in walking apertur

lambda = c / f0;       % Wavelength                                [m]
pitch = lambda / 2;    % Pitch - center-to-center                  [m]
width = .95*pitch;     % Width of the element                      [m]
kerf = pitch - width;  % Inter-element spacing                     [m]
height = 10/1000;      % Size in the Y direction                   [m]

% rx_fnum         = 0; % 2.1;   % Receive f-number - Set to 0 to turn off
                       % constants F-number reconstruction

%  Define the impulse response of the transducer
impulse_response = sin(2*pi*f0*(0:1/fs:1/f0));
impulse_response = impulse_response.*hanning(length(impulse_response))';

%  Define the phantom
pht_pos = [0 0 50/1000]; % Position
pht_amp = 20; % Strength of scattering

% Calculate minimum and maximum samples and times for phantom
Rmax = max(sqrt(pht_pos(:,1).^2 + pht_pos(:,2).^2  + pht_pos(:,3).^2)) + 5/1000;
Rmin = min(sqrt(pht_pos(:,1).^2 + pht_pos(:,2).^2  + pht_pos(:,3).^2)) - 5/1000;
if (Rmin < 0) Rmin = 0; end;

Tmin = 2*Rmin / c; Tmax = 2*Rmax / c;
Smin = floor(Tmin * fs); Smax = ceil(Tmax * fs);
max_code_length = max(cellfun(@(x) max(length(x.code), length(x.ccode)), codes));
Smin_c = Smin; Smax_c = Smax + max_code_length + 1000;

no_rf_samples = Smax - Smin + 1;
no_rf_samples_c = Smax_c - Smin_c + 1;

%  Initialize Field II and BFT
field_init(-1);
bft_init;

%  Set Field and BFT parameters
set_field('c', c);
bft_param('c', c);

set_field('fs', fs);
bft_param('fs', fs);

% Create transmitting and receiving apertures
xmt = xdc_linear_array(no_elements,width,height,kerf,1,1,[0 0 0]);
rcv = xdc_linear_array(no_elements,width,height,kerf,1,1,[0 0 0]);

% Create beamforming array
xdc = bft_linear_array(no_elements, width, kerf);

% Set the impulse responses
xdc_impulse(rcv, impulse_response);
xdc_impulse(xmt, impulse_response);

%% Imaging preparation (ex. line setup)

% Don't receive focus
xdc_focus_times(rcv, 0, zeros(1,no_elements));

% Set up the properties of each line we beamform along
no_lines = 257; % Number of lines to image along

d_x_line = width*no_elements / (no_lines-1); % Line width
x_line = -(no_lines-1) / 2 * d_x_line; % Current position of line
x_start = x_line; % Position of leftmost line 
z_points = linspace(Rmin, Rmax, 100); % For constant F number
no_points = length(z_points);
T = z_points/c*2;

%% New imaging loop
% We transmit the first code in each pair, then the second in each pair
no_Transmits = 2;
transmitResults = cell(2,1);
transmitResults{1} = zeros(no_rf_samples,no_lines);
transmitResults{2} = zeros(no_rf_samples,no_lines);

for currTransmit = 1:1 %no_Transmits
    % For each transmit, we image and beamform each line
    for currLine = 1:no_lines
        % Calculate and set apodization for this line
        xFocus = x_start + (currLine-1)*d_x_line; % Focus at [xFocus, 0, 0] for this line
        [apo_vector_tx,apo_vector_rx] = getApodization(xFocus,no_active_tx,no_active_rx,width, kerf, no_elements);
        xdc_apodization(xmt, 0, apo_vector_tx);
        xdc_apodization(rcv, 0, apo_vector_rx);
        
        % Set center reference point for transmit focus for this line
        xdc_center_focus(xmt,[xFocus, 0, 0])
        
        % For each line, we fire each relevant and store the results
        % Currently all codes in this transmit are fired for each line
        no_CodePairs = length(codes);
        RF_runningTot = 0; % Add the results from each code to this total
        for currCode = 1:no_CodePairs  
            % Get current code, and set as transducer excitation
            if (currTransmit == 1)
                code = codes{currCode}.code;
            else
                code = codes{currCode}.ccode;   
            end            
            xdc_excitation(xmt, code);
            
            % Set the transmit focus for this code
            xdc_focus(xmt, 0, codes{currCode}.focus);
            
            % Get RF data for transducers that fire
            [currCodeRF, start_time] = calc_scat_multi(xmt, rcv, pht_pos, pht_amp); 
            
             % Make sure the RF data exists exactly during the desired times
            currCodeRF = alignRF(currCodeRF,start_time,fs,Smin_c,Smax_c,no_rf_samples_c,no_elements);           
                        
            % Add to the RF running total for this line
            RF_runningTot = RF_runningTot + currCodeRF;
        end
        
        % Decode the data for this line
        % (cross correlate with each code in this transmit and add to a running total)
        currLineDec = 0;
        for currCode = 1:no_CodePairs
            % Get current code
            if (currTransmit == 1)
                code = codes{currCode}.code;
            else
                code = codes{currCode}.ccode;   
            end 
            % Cross correlate with current code
            currCodeDec = conv2(RF_runningTot, rot90(conj(code'), 2), 'valid');
            currCodeDec = currCodeDec(1:no_rf_samples, :);  
            
            % Add results to running total (over all codes used)
            currLineDec = currLineDec + currCodeDec;
        end
        
        % Beamform this line
        bft_center_focus([xFocus 0 0]); % Set up start of line for dynamic focus
        bft_dynamic_focus(xdc, 0, 0);  % Set direction of dynamic focus
        bft_apodization(xdc, 0, apo_vector_rx); % Beamforming apodization
        currLineBf = bft_beamform(Tmin, currLineDec); % Beamform
        
        % Add the beamformed line to the results for this transmit
        transmitResults{currTransmit}(:,currLine) = currLineBf + transmitResults{currTransmit}(:,currLine);
    end
end

% Add the transmit matrices to create a beamformed imaged
% (not normalized, not enveloped)
bfImag = plus(transmitResults{:});

% Normalize and do envelope detection
env_bf = abs(bfImag); %abs(hilbert(bfImag));
env_bf = env_bf / max(max(env_bf));

%% Calculate full width half maximum resolution
% (Needs generalization to work with a several point phantom)
% Find the index of the maximum signal
[maxVal,maxLocLin] = max(env_bf(:));
[maxLocZ, maxLocX] = ind2sub(size(env_bf),maxLocLin);

% Axial (z direction)
zIntVect = env_bf(:,maxLocX);
fwhmIndicesZ = fullWidthHM(zIntVect); % FWHM in indices
lenPerIndexZ = (Rmax - Rmin)/numel(zIntVect);
fwhmLengthZ = lenPerIndexZ*fwhmIndicesZ; % Convert to length
disp('Axial full width half maximum: (mm)')
disp(fwhmLengthZ*1000)

% Lateral (x direction)
xIntVect = env_bf(maxLocZ,:);
fwhmIndicesX = fullWidthHM(xIntVect); % FWHM in indices
lenPerIndexX = (no_lines*d_x_line)/numel(xIntVect);
fwhmLengthX = lenPerIndexX*fwhmIndicesX; % Convert to length
disp('Lateral full width half maximum: (mm)')
disp(fwhmLengthX*1000)

%% Plot image
figure;
imagesc([-1/2 1/2]*no_lines*d_x_line*1000, [Rmin Rmax]*1000, 20*log10(env_bf+eps));
title('Beamformed Image');
xlabel('Lateral distance [mm]');
ylabel('Axial distance [mm]')
axis('image')

colorbar
colormap(gray);
caxis([-55 0]);
