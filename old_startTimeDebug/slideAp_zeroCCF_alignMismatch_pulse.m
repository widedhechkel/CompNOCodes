% We process only one transmit focus at a time
% (fire, dedcode, beamform)
% (to avoid any cross correlation issues)

% We don't use codes here

% We manually focus

clear
%figure
addpath('C:\Users\User\Dropbox\Grad_School\Summer Codes\GitCodes\Field_II_Scripts\WalkingApertureCompl')

% Set the transmit focus locations (each row is an (x,y,z) point)
zTransDepth = 40e-3; % (m)
xTransFocusVals = [-10e-3:1e-4:3e-3]; %$[-15:15]*1e-3;% %-10e-3:2e-4:-6e-3; %-6e-3:1e-4:6e-3; % (m)
transFocLocs = zeros(numel(xTransFocusVals), 3);
transFocLocs(:,3) = zTransDepth;
transFocLocs(:,1) = xTransFocusVals;
numTransFocLocs = size(transFocLocs,1);

%%  Define the phantom
pht_pos = [ -3/1000 0 40/1000
            %-8/1000 0 44/1000
                ];
pht_amp = 20*ones(size(pht_pos,1),1); % Strength of scattering

%% Initial setup (ex. transducer, phantom, image range properties) 

% Transducer properties
f0 = 5e6;              %  Central frequency                        [Hz]
fs = 100e6;            %  Sampling frequency                       [Hz]
c = 1540;              %  Speed of sound                           [m/s]
no_elements = 192;     %  Number of elements in the transducer     

lambda = c / f0;       % Wavelength                                [m]
pitch = lambda / 2;    % Pitch - center-to-center                  [m]
width = .95*pitch;     % Width of the element                      [m]
kerf = pitch - width;  % Inter-element spacing                     [m]
height = 10/1000;      % Size in the Y direction                   [m]

%  Define the impulse response of the transducer
impulse_response = sin(2*pi*f0*(0:1/fs:1/f0));
impulse_response = impulse_response.*hanning(length(impulse_response))';

% Define excitation supplied to transducer
excitation = impulse_response;

x = cell(0);
x{1}.code = excitation;
x{1}.ccode = excitation;
% Calculate minimum and maximum samples and times for phantom
 [Rmax, Rmin, Tmin, Smin, max_code_length, Smin_c,Smax_c, no_rf_samples, no_rf_samples_c] =...
    calcSampleTimeRanges(pht_pos, x, c, fs);

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

%% Imaging preparation (ex. sliding aperture setup, receive focus, beamforming lines)

% Don't receive focus using Field II (will use beamforming toolbox)
xdc_focus_times(rcv, 0, zeros(1,no_elements));

% Set the size of the sliding aperture
wSlide = 5e-3; % width (m)
no_elSlide = round((wSlide + kerf) / (width + kerf)); % Number of elements in sliding aperture
no_active_tx = no_elSlide;
no_active_rx = no_elSlide;

%% Imaging

% Focus on each transmit focus location
% (we process these one at a time to get a zero cross correlation image)
no_lines = size(transFocLocs,1);
bfLines = zeros(no_rf_samples,no_lines);

% Get position in x of transmit elements
xmt_info = xdc_get(xmt, 'rect');
xElements = xmt_info(8, :);

for transFocInd = 1:no_lines % We image each line seperately
    % Get x location to transmit focus in
    transFoc = transFocLocs(transFocInd,:);
    xFocus = transFoc(1);

    % Calculate aperture directly below this transmit focus
    [apo_vector_tx,apo_vector_rx] = getApodization(xFocus,no_active_tx,no_active_rx,width, kerf, no_elements);
    
    % Set transmit and receive apodization as this aperture
    xdc_apodization(xmt, 0, apo_vector_tx);
    %xdc_apodization(rcv, 0, ones(1, no_elements)); % Receive on entire aperture
    xdc_apodization(rcv, 0, apo_vector_rx); % Receive on same aperture as for transmit
    
    %% Transmit focus
    tfManual = 1;
    if(tfManual == 1)
        % Transmit focus - manual    
        xdc_focus_times(xmt, 0, zeros(1, no_elements));
        xdc_focus_times(rcv, 0, zeros(1, no_elements));

        % Manually specify focusing delays              
        startEle = find(apo_vector_rx,1,'first');
        endEle = find(apo_vector_rx,1,'last');

        beam = zeros(1500, no_elements);

        forceMaxDelay = 100;
        xEle = xElements(floor(no_elements/2-no_active_tx/2+1):floor(no_elements/2+no_active_tx/2));
        [tmpBeam, maxDelay] = focusBeam(excitation, [0 0 transFoc(3)], xEle, fs, c, forceMaxDelay);
        beam(1:size(tmpBeam, 1), startEle:endEle) = beam(1:size(tmpBeam, 1), startEle:endEle) + tmpBeam;
        beam = beam(1:ceil(maxDelay*fs+21), :, :);   
        
        ele_waveform(xmt, (1:no_elements)', beam');    
    else
        % Transmit focus - use Field II
        xdc_focus(xmt, 0, transFoc)
        xdc_excitation(xmt, excitation);
    end

    %% Fire beam and get raw RF scattering data
    [RF, start_timeCode] = calc_scat_multi(xmt, rcv, pht_pos, pht_amp); 

    %% Crop / pad data to occur at the right time
    RFAligned = alignRF(RF, start_timeCode,fs,Smin_c,Smax_c,no_rf_samples_c,no_elements);            
    RFAligned = RFAligned(1:no_rf_samples, :);
    
    %% Beamform the data from this aperture
    
    % Set receive focus
    currRe = transFoc; 
    focusX = currRe(1);
    
    % Set beamforming apodization to be aperture directly below scatterer
    % (also used for transmit and receive)
    %bft_apodization(xdc, 0, apo_vector_rx);    
    bft_apodization(xdc, 0, ones(1, no_elements));
    
    % Set current receive focus        
    bft_center_focus([xFocus 0 0]); % Set up start of line for dynamic focus       
    bft_dynamic_focus(xdc, 0, 0);    % Set direction of dynamic focus  
    
    % Beamform and store resulting line
    % Update Tmin to be correct!    
    %tFirstSample = manualTrim*(1/fs);
    %currLineBf = bft_beamform(Tmin, sumDecAp);
    if(tfManual == 1)
        currLineBf = bft_beamform(Tmin-maxDelay, RFAligned); 
    else
        currLineBf = bft_beamform(Tmin, RFAligned); 
    end
      
    bfLines(:,transFocInd) = currLineBf;
end 

if(tfManual == 1)
    Rmin = Rmin -maxDelay*c/2;
    Rmax = Rmax -maxDelay*c/2; 
end
% We have imaged each line

% Normalize and do envelope detection
env_bf = abs(hilbert(bfLines));
env_bf = env_bf / max(max(env_bf));
toPlot = 20*log10(env_bf+eps);

% Plot current line
figure
imagesc([min(transFocLocs(:,1)) max(transFocLocs(:,1)) ]*1000, ([Rmin Rmax])*1000, toPlot);
title(sprintf('One Subaperture Per Line. Manual Focusing. Receive on Subaperture.'),'Interpreter','None');
xlabel('Lateral distance [mm]');
ylabel('Axial distance [mm]')
axis('image')

colorbar
colormap(gray);
caxis([-55 0]);

% Release memory
field_end
bft_end

