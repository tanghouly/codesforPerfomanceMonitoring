% --- FULL SPORTS MONITOR SUITE (CASCADING WINDOWS) ---
clc; clear; close all;

% --- SETTINGS ---
comPort = 'COM10';  % <--- CHECK THIS MATCHES YOUR ESP32
baudRate = 115200;

% =========================================================
% PART 1: AUTO-DOWNLOADER
% =========================================================
fprintf('=================================================\n');
fprintf('STATUS: Connecting to %s...\n', comPort);

try
    s = serialport(comPort, baudRate);
    configureTerminator(s, "CR/LF");
    flush(s);
    
    % Trigger Dump Mode
    fprintf('STATUS: Sending Dump Command (D)...\n');
    pause(1);          
    writeline(s, "D"); 

    fileID = -1;
    downloadedFiles = {}; 

    fprintf('STATUS: Listening for files...\n');
    while true
        if s.NumBytesAvailable > 0
            line = readline(s);
            
            % Detect Start of File
            if contains(line, "=== START OF FILE:")
                parts = split(line, ": ");
                rawName = replace(parts(2), ["/", " ===", "\r", "\n"], ""); 
                fName = char(strtrim(rawName));
                fprintf('--> Downloading: %s ... ', fName);
                fileID = fopen(fName, 'w');
                downloadedFiles{end+1} = fName;
            
            % Detect End of File
            elseif contains(line, "=== END OF DATA ===")
                if fileID > 0, fclose(fileID); end
                fprintf('Done.\n');
                
            % Detect End of All Dumps
            elseif contains(line, "ALL FILES DUMPED")
                fprintf('>>> SUCCESS! All sessions saved.\n');
                break;
                
            % Write Data Lines
            elseif fileID > 0 && contains(line, ",")
                fprintf(fileID, '%s\n', line);
            end
        end
    end
    clear s;
    
catch e
    fprintf('NOTE: %s (Assuming files are already downloaded)\n', e.message);
    downloadedFiles = dir('session_*.csv');
    downloadedFiles = {downloadedFiles.name};
end

if isempty(downloadedFiles)
    errordlg('No files found! Check USB connection.', 'Error');
    return;
end

% =========================================================
% PART 2: ANALYSIS MENU
% =========================================================
choice = questdlg('Download Complete.', 'Analysis Mode', 'Graph ALL', 'Graph ONE', 'Graph ALL');

switch choice
    case 'Graph ALL'
        % Loop through files and pass 'i' to offset the windows
        for i = 1:length(downloadedFiles)
            analyzeSession(downloadedFiles{i}, i);
        end
    case 'Graph ONE'
        [file, path] = uigetfile('session_*.csv', 'Select a Session');
        if ~isequal(file, 0)
            analyzeSession(fullfile(path, file), 1);
        end
end

% =========================================================
% PART 3: ANALYSIS FUNCTION (WITH CASCADING & LABELS)
% =========================================================
function analyzeSession(filename, windowIndex)
    data = readtable(filename);
    if isempty(data), return; end

    % Time Conversion
    if ismember('Time', data.Properties.VariableNames)
        timeSec = (data.Time - data.Time(1)) / 1000;
    else
        timeSec = 1:height(data);
    end

    % --- CALCULATE STATISTICS ---
    topSpeed = max(data.SpeedZ);
    avgSpeed = mean(data.SpeedZ);
    maxFwdAccel = max(abs(data.AccFwd)); 
    avgFwdAccel = mean(abs(data.AccFwd));
    maxVertAccel = max(abs(data.AccUp));
    
    validBPM = data.BPM(data.BPM > 40); 
    if isempty(validBPM)
        avgBPM = 0; maxBPM = 0;
    else
        avgBPM = mean(validBPM);
        maxBPM = max(validBPM);
    end

    % --- CLEAN FILENAME FOR DISPLAY ---
    % Removes ".csv" and "session_" to just show "SESSION 1"
    cleanName = upper(strrep(strrep(filename, '.csv', ''), '_', ' '));
    % If full path is passed, just get the name
    [~, name, ~] = fileparts(cleanName);
    cleanName = upper(strrep(name, '_', ' '));

    % --- 1. SHOW POPUP STATS WINDOW (Cascaded) ---
    % Offset position based on windowIndex so they don't stack
    xPos = 800 + (windowIndex * 30);
    yPos = 500 - (windowIndex * 30);
    
    msgText = { ...
        ['REPORT: ' cleanName], ...
        '---------------------------------------', ...
        ['Top Speed:       ' num2str(topSpeed, '%.2f') ' m/s'], ...
        ['Average Speed:   ' num2str(avgSpeed, '%.2f') ' m/s'], ...
        ' ', ...
        ['Peak Forward:    ' num2str(maxFwdAccel, '%.2f') ' m/s^2'], ...
        ['Peak Vertical:   ' num2str(maxVertAccel, '%.2f') ' m/s^2'], ...
        ' ', ...
        ['Max Heart Rate:  ' num2str(maxBPM, '%.0f') ' BPM'], ...
        ['Avg Heart Rate:  ' num2str(avgBPM, '%.0f') ' BPM'] ...
    };
    
    % Note: msgbox positioning is tricky, we rely on the title to distinguish
    h = msgbox(msgText, ['STATS: ' cleanName]);

    % --- 2. CREATE GRAPHS WINDOW (Cascaded) ---
    graphX = 50 + (windowIndex * 30);
    graphY = 50 - (windowIndex * 30);
    
    f = figure('Name', ['Graphs: ' cleanName], 'Color', 'w', ...
               'Position', [graphX graphY 800 900]);
    
    % *** SUPER TITLE ***
    sgtitle(cleanName, 'FontSize', 16, 'FontWeight', 'bold', 'Color', 'b');
    
    subplot(5,1,1); plot(timeSec, data.Tilt, 'k'); 
    title('Body Lean (Tilt)'); ylabel('Deg'); grid on;
    
    subplot(5,1,2); plot(timeSec, data.AccFwd, 'r'); 
    title('Forward Accel (m/s^2)'); grid on;
    
    subplot(5,1,3); plot(timeSec, data.AccUp, 'b'); 
    title('Vertical Accel (m/s^2)'); grid on;
    
    subplot(5,1,4); area(timeSec, data.SpeedZ, 'FaceColor', 'g', 'FaceAlpha', 0.3); 
    title('Speed (m/s)'); grid on;
    
    subplot(5,1,5); plot(timeSec, data.BPM, 'm', 'LineWidth', 2); 
    title('Heart Rate (BPM)'); grid on; ylim([40 220]);
end