// Private implementation fragment, included once by Simulation2D.cpp.
inline std::filesystem::path vtiPath2D(const char* path) {
    auto result=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path)));
    return result.is_absolute()?result:getExecutableDirectory()/result;
}
inline void exportVtiModel2D(const std::filesystem::path& path) {
    if(acquisition2D.isRecording||autoAcquisition2D.state==AUTO_ACQ_RUNNING) throw std::runtime_error("Stop acquisition before saving a model.");
    const auto& c=simulationContext2D;
    if(c.total_grid<=0) throw std::runtime_error("Initialize a model first.");
    SeisCommon::ModelGrid g;g.nx=c.NX;g.ny=1;g.nz=c.NZ;g.spacing={c.h,c.h,c.h};
    g.metadata={{"Dimension",2},{"SolverScheme",c.flag_type},{"CPMLThickness",c.npml},{"BoundaryMode",c.flag_type==3&&c.upFlag?1:0}};
    SeisCommon::writeVtiModel(path,g,[&](int z,float* out) {
        for(int x=0;x<c.NX;++x) {
            const auto q=z*c.NX+x;out[3*x]=std::sqrt(c.lambda2mu[q]/c.rho[q]);out[3*x+1]=std::sqrt(c.mu[q]/c.rho[q]);out[3*x+2]=c.rho[q];
        }
    });
}
inline void importVtiModel2D(const std::filesystem::path& path,GLHandles& gl,SimState& state) {
    if(acquisition2D.isRecording||autoAcquisition2D.state==AUTO_ACQ_RUNNING) throw std::runtime_error("Stop acquisition before loading a model.");
    SeisCommon::VtiModelReader reader(path);const auto& g=reader.grid();
    if(g.ny!=1||g.nx<64||g.nz<64||g.nx>8192||g.nz>8192) throw std::runtime_error("2D VTI requires NX x 1 x NZ, with X/Z dimensions in 64..8192.");
    if(g.origin!=std::array<double,3>{0,0,0}||std::abs(g.spacing[0]-g.spacing[2])>g.spacing[0]*1e-7)
        throw std::runtime_error("2D VTI requires origin (0,0,0) and equal X/Z spacing.");
    int scheme=simulationContext2D.flag_type,pml=simulationContext2D.npml;
    bool free=simulationContext2D.upFlag;
    if(auto p=g.metadata.find("SolverScheme");p!=g.metadata.end()) scheme=p->second;
    if(auto p=g.metadata.find("CPMLThickness");p!=g.metadata.end()) pml=p->second;
    if(auto p=g.metadata.find("BoundaryMode");p!=g.metadata.end()) {if(p->second>1) throw std::runtime_error("Unsupported 2D boundary mode.");free=p->second==1;}
    if(scheme<1||scheme>3||pml<0||pml>500||(scheme==3&&free&&pml<kStencilRadius)) throw std::runtime_error("Invalid 2D boundary parameters.");
    const float h=float(g.spacing[0]);if(!std::isfinite(h)||h<=0) throw std::runtime_error("Invalid 2D spacing.");
    // Complete the bounded read before changing the active model; retain physical top-origin Z order.
    std::vector<float> values(std::size_t(g.nx)*g.nz*3);
    for(int z=0;z<g.nz;++z) reader.readPlane(z,values.data()+std::size_t(z)*g.nx*3);
    state.running=false;currentIt2D=0;elapsedRunWallTime2D=0;activeSources2D.clear();
    gridWidth2D=g.nx;gridHeight2D=g.nz;parameters2D.model.xnum=g.nx;parameters2D.model.znum=g.nz;
    parameters2D.model.dx=parameters2D.model.dz=h;parameters2D.FDM.npml=float(pml);parameters2D.FDM.upFlag=free?1.f:0.f;
    simulationContext2D.flag_type=scheme;setupTestContext(simulationContext2D,parameters2D);
    for(int z=0;z<g.nz;++z) for(int x=0;x<g.nx;++x) {const auto q=(std::size_t(z)*g.nx+x)*3;setGridMaterial2D(x,z,values[q],values[q+1],values[q+2]);}
    const float previousDt=simulationContext2D.dt;autoAlignTimeStep();
    if(previousDt>0&&previousDt<=simulationContext2D.dt) simulationContext2D.dt=previousDt;
    tempDt2D=simulationContext2D.dt;parameters2D.FDM.dt=tempDt2D;
    generateRickerWavelet(simulationContext2D.wavelet,simulationContext2D.nt,simulationContext2D.dt,sourceFrequency2D,sourceDelay2D);
    editSrcX2D=g.nx/2;editSrcZ2D=g.nz/4;simulationContext2D.src_z_idx=editSrcZ2D;
    simulationContext2D.src_idx=editSrcZ2D*g.nx+editSrcX2D;updatePmlDampingArrays();
    releaseGpuSimulation(gpuData2D);
    if(gl.cudaSeisRes) cudaGraphicsUnregisterResource(gl.cudaSeisRes);gl.cudaSeisRes=nullptr;
    glBindTexture(GL_TEXTURE_2D,gl.seisTex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,g.nx+2*domainPad2D(),g.nz+2*domainPad2D(),0,GL_RGBA,GL_FLOAT,nullptr);
    initializeExternalSimulation2D(gpuData2D,simulationContext2D);uploadMaterialToGpu();
    acquisition2D.receivers.erase(std::remove_if(acquisition2D.receivers.begin(),acquisition2D.receivers.end(),[&](const auto& r){return r.x<0||r.x>=g.nx||r.y<0||r.y>=g.nz;}),acquisition2D.receivers.end());
    firstAlignNeeded2D=true;resetSimRequested2D=true;
}
inline bool browseVtiModel2D(char* field,std::size_t capacity,bool save) {
#ifdef _WIN32
    std::filesystem::path selected;
    if(!SeisCommon::chooseNativeFile(vtiPath2D(field),getExecutableDirectory(),
            L"VTK ImageData model (*.vti)\0*.vti\0",L"vti",save,selected)) return false;
    const auto result=pathToUtf8(selected);
    if(result.size()>=capacity) throw std::runtime_error("Model path too long.");
    std::memcpy(field,result.c_str(),result.size()+1);return true;
#else
    return false;
#endif
}
inline void drawVtiModelControls2D(GLHandles& gl,SimState& state) {
    if(!parameterGroup2D("VTI MODEL (PRIMARY FORMAT)")) return;
    static char input[1024]{},output[1024]{};
    if(!input[0]) {const auto p=pathToUtf8(getExecutableDirectory()/"data_Seis2D/model.vti");std::snprintf(input,sizeof(input),"%s",p.c_str());}
    if(!output[0]) {const auto p=pathToUtf8(getExecutableDirectory()/"data_Seis2D/model_export.vti");std::snprintf(output,sizeof(output),"%s",p.c_str());}
    AppUi::Widgets::TextWrapped("One VTI file stores Vp, Vs, Density and physical spacing. SEG-Y groups remain available for external exchange.");
    auto action=[&](auto fn){try{fn();}catch(const std::exception& e){popupMessage2D=e.what();showErrorPopup2D=true;}};
    AppUi::Widgets::InputText("VTI input path",input,sizeof(input));
    if(AppUi::Widgets::Button("Browse VTI model...",ImVec2(-1,0))) action([&]{browseVtiModel2D(input,sizeof(input),false);});
    if(AppUi::Widgets::Button("LOAD VTI MODEL",ImVec2(-1,0))) action([&]{importVtiModel2D(vtiPath2D(input),gl,state);popupMessage2D="VTI model loaded; wavefield reset.";showSuccessPopup2D=true;});
    AppUi::Widgets::InputText("VTI output path",output,sizeof(output));
    if(AppUi::Widgets::Button("Choose VTI model output...",ImVec2(-1,0))) action([&]{browseVtiModel2D(output,sizeof(output),true);});
    if(AppUi::Widgets::Button("SAVE VTI MODEL",ImVec2(-1,0))) action([&]{exportVtiModel2D(vtiPath2D(output));popupMessage2D="VTI physical model saved.";showSuccessPopup2D=true;});
    AppUi::Widgets::TextWrapped("VTI uses SI units and physical nodes only. Existing files are preserved. Loading resets the wavefield.");
}

// Select a legacy 2D model input path without changing simulation state.
inline bool openSystemFileDialog(char* buffer, int bufferSize,bool model=true) {
    memset(buffer, 0, bufferSize);

#ifdef _WIN32

    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = bufferSize;
    ofn.lpstrFilter = model?"SEG-Y Model (*.sgy;*.segy)\0*.sgy;*.segy\0":"Wavefield movie\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameA(&ofn);
#else

    const char* cmd = "zenity --file-selection --file-filter='SEG-Y | *.sgy *.segy' --title='Select Model Data'";
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        std::cerr << "[IO Error] Zenity not found. Please install it to use the file dialog." << std::endl;
        return false;
    }
    bool success = false;
    if (fgets(buffer, bufferSize, pipe) != NULL) {
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
        success = (strlen(buffer) > 0);
    }
    pclose(pipe);
    return success;
#endif
}

#ifdef _WIN32
inline bool saveSystemFileDialog(char* outPath, DWORD maxPathLen) {
    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = outPath;
    ofn.lpstrFile[0] = '\0';
    ofn.nMaxFile = maxPathLen;
    ofn.lpstrFilter = "Seismic SEGY (*.sgy)\0*.sgy;*.segy\0All Files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;

    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    return GetSaveFileNameA(&ofn) == TRUE;
}
#else
inline bool saveSystemFileDialog(char* outPath, size_t maxPathLen) {
    return false;
}
#endif

// Resamples a 2D material grid to the requested dimensions using interpolation.
inline std::vector<std::vector<float>> resampleGrid2d(
    const std::vector<std::vector<float>>& input,
    float                                  target_dx,
    float                                  original_dx
) {
    if (input.empty() || input[0].empty()) return {};

    int old_nx = static_cast<int>(input.size());
    int old_nz = static_cast<int>(input[0].size());

    float physical_width = (old_nx - 1) * original_dx;
    float physical_depth = (old_nz - 1) * original_dx;

    int new_nx = static_cast<int>(physical_width / target_dx) + 1;
    int new_nz = static_cast<int>(physical_depth / target_dx) + 1;

    std::cout << "[Resample] Resampling grid: " << old_nx << "x" << old_nz
        << " -> " << new_nx << "x" << new_nz
        << " (spacing: " << original_dx << "m -> " << target_dx << "m)" << std::endl;

    std::vector<std::vector<float>> output(new_nx, std::vector<float>(new_nz, 0.0f));
    float ratio = target_dx / original_dx;

    for (int x = 0; x < new_nx; ++x) {
        int old_x = static_cast<int>(x * ratio + 0.5f);
        if (old_x >= old_nx) old_x = old_nx - 1;

        for (int z = 0; z < new_nz; ++z) {
            int old_z = static_cast<int>(z * ratio + 0.5f);
            if (old_z >= old_nz) old_z = old_nz - 1;

            output[x][z] = input[old_x][old_z];
        }
    }
    return output;
}

// Loads velocity/material samples from SEG-Y and updates the selected model state.
inline bool loadModelFromSegy(
    const std::string& vp_path,
    const std::string& vs_path,
    const std::string& rho_path,
    bool               flipVertically,
    bool               flipHorizontally,
    GLHandles& gl,
    SimState& state
) {
    for(const std::string path:{vp_path,vs_path,rho_path}) {
        auto extension=std::filesystem::path(path).extension().string();
        std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(extension!=".sgy"&&extension!=".segy") return false;
    }

    std::cout << "[IO] Loading SEGY model files..." << std::endl;

    auto raw_vp = SeismicIO::readSegyFile2D(vp_path);
    auto raw_vs = SeismicIO::readSegyFile2D(vs_path);
    auto raw_rho = SeismicIO::readSegyFile2D(rho_path);

    if (raw_vp.empty() || raw_vs.empty() || raw_rho.empty()) {
        std::cerr << "[IO] Error: One or more SEGY files failed to load." << std::endl;
        return false;
    }

    float original_segy_dx = editSegyDx2D;
    float target_system_dx = parameters2D.model.dx;

    auto grid_vp = resampleGrid2d(raw_vp, target_system_dx, original_segy_dx);
    auto grid_vs = resampleGrid2d(raw_vs, target_system_dx, original_segy_dx);
    auto grid_rho = resampleGrid2d(raw_rho, target_system_dx, original_segy_dx);

    auto vp_pad = grid_vp;
    auto vs_pad = grid_vs;
    auto rho_pad = grid_rho;
    if(convertRho2D) for(auto& column:rho_pad) for(float& rho:column) {
        if(rho>0&&rho<10) rho*=1000;
        if(rho<10) rho=1000;
    }

    int file_w = static_cast<int>(vp_pad.size());
    int file_h = static_cast<int>(vp_pad[0].size());

    state.running = false;
    currentIt2D = 0;
    elapsedRunWallTime2D = 0.0f;
    activeSources2D.clear();

    gridWidth2D = file_w;
    gridHeight2D = file_h;
    parameters2D.model.xnum = gridWidth2D;
    parameters2D.model.znum = gridHeight2D;

    setupTestContext(simulationContext2D, parameters2D);

    for (int x = 0; x < simulationContext2D.NX; ++x) {
        for (int z = 0; z < simulationContext2D.NZ; ++z) {
            float vp = vp_pad[x][z];
            float vs = vs_pad[x][z];
            float rho = rho_pad[x][z];

            int tgtX = x;
            if (flipHorizontally) tgtX = simulationContext2D.NX - 1 - x;

            int tgtZ = z;
            if (flipVertically)   tgtZ = simulationContext2D.NZ - 1 - z;

            setMaterialAt(tgtX, tgtZ, vp, vs, rho);
        }
    }

    simulationContext2D.src_z_idx = simulationContext2D.NZ / 4;
    simulationContext2D.src_idx = simulationContext2D.src_z_idx * simulationContext2D.NX + (simulationContext2D.NX / 2);
    editSrcX2D = simulationContext2D.NX / 2;
    editSrcZ2D = simulationContext2D.NZ / 4;

    simulationContext2D.dp_flat.assign(simulationContext2D.NX, 0.0f);
    int fs_idx = simulationContext2D.npml;
    for (int j = 0; j < simulationContext2D.NX; ++j) {
        int k = fs_idx * simulationContext2D.NX + j;
        if (k < simulationContext2D.total_grid && simulationContext2D.lambda2mu[k] > 0.0f) {
            float l2m = simulationContext2D.lambda2mu[k];
            float lam = simulationContext2D.lambda[k];
            simulationContext2D.dp_flat[j] = (l2m * l2m - lam * lam) / l2m;
        }
    }
    updatePmlDampingArrays();
    autoAlignTimeStep();

    releaseGpuSimulation(gpuData2D);
    if(gl.cudaSeisRes) cudaGraphicsUnregisterResource(gl.cudaSeisRes);gl.cudaSeisRes=nullptr;
    glBindTexture(GL_TEXTURE_2D, gl.seisTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, simulationContext2D.NX+2*domainPad2D(), simulationContext2D.NZ+2*domainPad2D(), 0, GL_RGBA, GL_FLOAT, nullptr);


    initializeExternalSimulation2D(gpuData2D, simulationContext2D);

    updateModelTexture();
    recalculateCachedModelMetrics();
    firstAlignNeeded2D = true;

    return true;
}

// Writes the current material model to SEG-Y with the selected axis metadata.
inline bool exportModelToSegy(
    const std::string& base_filepath,
    bool               density_gcm3,
    bool               flipHorizontally,
    int                x_min,
    int                z_min,
    int                x_max,
    int                z_max,
    float              target_dx
) {
    if (simulationContext2D.total_grid <= 0) return false;

    x_min = std::clamp(x_min, 0, simulationContext2D.NX - 1);
    x_max = std::clamp(x_max, 0, simulationContext2D.NX - 1);
    z_min = std::clamp(z_min, 0, simulationContext2D.NZ - 1);
    z_max = std::clamp(z_max, 0, simulationContext2D.NZ - 1);

    if (x_min > x_max) std::swap(x_min, x_max);
    if (z_min > z_max) std::swap(z_min, z_max);

    int new_NX = x_max - x_min + 1;
    int new_NZ = z_max - z_min + 1;

    std::cout << "[IO] Preparing SEGY Export with Cropping..." << std::endl;
    std::cout << "     - Crop Range: X[" << x_min << " ~ " << x_max << "], Z[" << z_min << " ~ " << z_max << "]" << std::endl;
    std::cout << "     - Raw Crop Size: " << new_NX << "x" << new_NZ << std::endl;

    std::vector<std::vector<float>> cropped_vp(new_NX, std::vector<float>(new_NZ, 0.0f));
    std::vector<std::vector<float>> cropped_vs(new_NX, std::vector<float>(new_NZ, 0.0f));
    std::vector<std::vector<float>> cropped_rho(new_NX, std::vector<float>(new_NZ, 0.0f));

    for (int x = 0; x < new_NX; ++x) {

        int targetX = flipHorizontally ? (x_max - x) : (x_min + x);

        for (int z = 0; z < new_NZ; ++z) {
            int targetZ = z_min + z;

            int k_cuda = targetZ * simulationContext2D.NX + targetX;

            float rho_val = simulationContext2D.rho[k_cuda];
            float vp_val = 0.0f;
            float vs_val = 0.0f;

            if (rho_val > 0.0f) {
                vp_val = sqrtf(simulationContext2D.lambda2mu[k_cuda] / rho_val);
                vs_val = sqrtf(simulationContext2D.mu[k_cuda] / rho_val);
            }

            cropped_vp[x][z] = vp_val;
            cropped_vs[x][z] = vs_val;

            if (density_gcm3) cropped_rho[x][z] = rho_val / 1000.0f;
            else              cropped_rho[x][z] = rho_val;
        }
    }

    std::vector<std::vector<float>> final_vp, final_vs, final_rho;
    int export_NX = new_NX;
    int export_NZ = new_NZ;

    if (target_dx > 0.0f && std::abs(target_dx - simulationContext2D.h) > 1e-4f) {
        std::cout << "     - [Trigger] Resampling sub-grid: " << simulationContext2D.h << "m -> " << target_dx << "m" << std::endl;
        final_vp = resampleGrid2d(cropped_vp, target_dx, simulationContext2D.h);
        final_vs = resampleGrid2d(cropped_vs, target_dx, simulationContext2D.h);
        final_rho = resampleGrid2d(cropped_rho, target_dx, simulationContext2D.h);

        export_NX = static_cast<int>(final_vp.size());
        export_NZ = static_cast<int>(final_vp[0].size());
    }
    else {
        final_vp = cropped_vp;
        final_vs = cropped_vs;
        final_rho = cropped_rho;
    }

    size_t             export_total_grid = static_cast<size_t>(export_NX) * export_NZ;
    std::vector<float> flat_vp(export_total_grid);
    std::vector<float> flat_vs(export_total_grid);
    std::vector<float> flat_rho(export_total_grid);

    for (int x = 0; x < export_NX; ++x) {
        for (int z = 0; z < export_NZ; ++z) {
            size_t k_segy = (size_t)x * export_NZ + z;
            flat_vp[k_segy] = final_vp[x][z];
            flat_vs[k_segy] = final_vs[x][z];
            flat_rho[k_segy] = final_rho[x][z];
        }
    }

    std::string clean_path = base_filepath;
    size_t      dot_pos = clean_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        clean_path = clean_path.substr(0, dot_pos);
    }
    std::string out_vp_path = clean_path + "_vp.sgy";
    std::string out_vs_path = clean_path + "_vs.sgy";
    std::string out_rho_path = clean_path + "_rho.sgy";

    float dummy_dt = 0.001f;
    SeismicIO::writeSegyFile2D(flat_vp, export_NX, export_NZ, out_vp_path, dummy_dt);
    SeismicIO::writeSegyFile2D(flat_vs, export_NX, export_NZ, out_vs_path, dummy_dt);
    SeismicIO::writeSegyFile2D(flat_rho, export_NX, export_NZ, out_rho_path, dummy_dt);

    std::cout << "[IO] Resampled SEGY Model Group Export Completed successfully." << std::endl;
    std::cout << "     - Final Output Size: " << export_NX << " traces x " << export_NZ << " samples" << std::endl;
    return true;
}

// Loads recorded seismic traces from SEG-Y into the analysis state.

// Extracts optional grid dimensions encoded in a model filename.
inline bool parseDimensionsFromPath(const std::string& path, int& out_w, int& out_z) {

    size_t last_x = path.find_last_of("xX");
    if (last_x == std::string::npos) return false;

    size_t start_idx = path.find_last_of('_', last_x);
    if (start_idx == std::string::npos) {
        start_idx = path.find_last_of("\\/", last_x);
        if (start_idx == std::string::npos) start_idx = 0;
        else                                start_idx += 1;
    }
    else {
        start_idx += 1;
    }

    size_t end_idx = path.find_first_of('_', last_x);
    if (end_idx == std::string::npos) {
        end_idx = path.find_last_of('.');
        if (end_idx == std::string::npos || end_idx < last_x) {
            end_idx = path.size();
        }
    }

    if (start_idx >= last_x || last_x >= end_idx) return false;

    std::string w_str = path.substr(start_idx, last_x - start_idx);
    std::string z_str = path.substr(last_x + 1, end_idx - last_x - 1);

    auto is_digits = [](const std::string& s) {
        return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
        };

    if (is_digits(w_str) && is_digits(z_str)) {
        out_w = std::stoi(w_str);
        out_z = std::stoi(z_str);
        return true;
    }
    return false;
}

// Synchronizes receiver coordinates with the GPU sampling buffers.
