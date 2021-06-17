/****************************************************************************
* MeshLab                                                           o o     *
* A versatile mesh processing toolbox                             o     o   *
*                                                                _   O  _   *
* Copyright(C) 2021                                                \/)\/    *
* JI-IN Systems.                                                  /\/|      *
*                                                                    |      *
* All rights reserved.                                               \      *
*                                                                           *
* This program is free software; you can redistribute it and/or modify      *
* it under the terms of the GNU General Public License as published by      *
* the Free Software Foundation; either version 2 of the License, or         *
* (at your option) any later version.                                       *
*                                                                           *
* This program is distributed in the hope that it will be useful,           *
* but WITHOUT ANY WARRANTY; without even the implied warranty of            *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             *
* GNU General Public License (http://www.gnu.org/licenses/gpl.txt)          *
* for more details.                                                         *
*                                                                           *
****************************************************************************/

#include <common/globals.h>
#include <common/mlapplication.h>
#include <common/mlexception.h>
#include <common/filterscript.h>
#include <common/parameters/rich_parameter_list.h>
#include <common/plugins/plugin_manager.h>
#include <common/utilities/load_save.h>

#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QGLFormat>

#include <clocale>
#include <filesystem>
#include <stdlib.h>


namespace commandline
{
	const char info_file_path('i');
    const char target_face_percent('f');
	const char target_quality('q');

    void usage()
    {
        printf("mesh simplification version: %s\n", qUtf8Printable(MeshLabApplication::appVer()));
        QFile file(":/mesh_simplification.txt");
        if (!file.open(QIODevice::ReadOnly))
        {
            printf("MeshLabServer was not able to locate meshlabserver.txt file. The program will be closed\n");
            exit(-1);
        }

        const QString help(file.readAll());
        printf("\nUsage:\n%s",qUtf8Printable(help));
        file.close();
    }

    QString option_value_expression(const char option)
    {
        //Validate an option followed by spaces and a filepath
        return QString ("-" + QString(option) + "\\s+(.+)");
    }
}

bool export_mesh(QString output_file_path, PluginManager& plugin_manager, MeshDocument& mesh_document, int texture_quality)
{
    bool saved = true;
    if (output_file_path.isEmpty())
    {
    	return false;
    }
	
    //save path away so we can use it again
    QString output_directory_path = output_file_path;
    output_directory_path.truncate(output_file_path.lastIndexOf("/"));

    QString extension = output_file_path;
    extension.remove(0, output_file_path.lastIndexOf('.') + 1);

    IOPlugin* p_io_plugin = plugin_manager.outputMeshPlugin(extension);
    if (p_io_plugin == nullptr)
    {
        return false;
    }
    p_io_plugin->setLog(&mesh_document.Log);

    MeshModel* p_mesh_model = mesh_document.mm();
    	
    int capability = 0;
    int default_bits = 0;
    p_io_plugin->exportMaskCapability(extension, capability, default_bits);
    const RichParameterList save_parameters = p_io_plugin->initSaveParameter(extension, *p_mesh_model);

    try
    {
	    const int mask = 4368;
	    p_io_plugin->save(extension, output_file_path, *p_mesh_model, mask, save_parameters, nullptr);
        p_mesh_model->saveTextures(output_directory_path, texture_quality);
    	
    	return true;
    }
    catch (const MLException& e)
    {
        return false;
    }
}

bool import_mesh(QString input_file_name, PluginManager& plugin_manager, MeshDocument& mesh_document)
{
    QStringList file_names;
    file_names.push_back(input_file_name);
    if (file_names.isEmpty())
    {
        return false;
    }

    QElapsedTimer total_time;
    total_time.start();
	
    for (const QString& file_name : file_names)
    {
        QFileInfo file_info(file_name);
        if (!file_info.exists())
        {
            //QString error_msg_format = "Unable to open file:\n\"%1\"\n\nError details: file %1 does not exist.";

            return false;
        }
        if (!file_info.isReadable())
        {
            //QString error_msg_format = "Unable to open file:\n\"%1\"\n\nError details: file %1 is not readable.";

            return false;
        }

        QString extension = file_info.suffix();
        IOPlugin* p_io_plugin = plugin_manager.inputMeshPlugin(extension);

        if (p_io_plugin == nullptr)
        {
            // QString error_msg_format("Unable to open file:\n\"%1\"\n\nError details: file format " + extension + " not supported.");

            return false;
        }

        p_io_plugin->setLog(&mesh_document.Log);
        RichParameterList pre_parameters = p_io_plugin->initPreOpenParameter(extension);

        const unsigned int mesh_count = p_io_plugin->numberMeshesContainedInFile(extension, file_name, pre_parameters);
        QFileInfo info(file_name);
        std::list<MeshModel*> mesh_model_ptrs;
        for (unsigned int i = 0; i < mesh_count; i++)
        {
            MeshModel* p_mesh_model = mesh_document.addNewMesh(file_name, info.fileName());
            if (mesh_count != 1)
            {
                p_mesh_model->setIdInFile(i);
            }
            mesh_model_ptrs.push_back(p_mesh_model);
        }

        try
        {
	        std::list<int> masks;
            std::list<std::string> unloaded_textures = meshlab::loadMesh(file_name, p_io_plugin, pre_parameters, mesh_model_ptrs, masks, nullptr);
        }
        catch (const MLException& e)
        {
            for (MeshModel* p_mesh_model : mesh_model_ptrs)
            {
            	mesh_document.delMesh(p_mesh_model);
            }
        }
    }
	
    return true;
}

RichParameterList build_simplification_parameters(MeshModel const& mesh_model, float target_face_ratio, float quality_threshold)
{
    RichParameterList result;

    result.addParam(RichInt("TargetFaceNum", (0 < mesh_model.cm.sfn) ? mesh_model.cm.sfn * target_face_ratio : mesh_model.cm.fn * target_face_ratio, "Target number of faces", "The desired final number of faces."));
    result.addParam(RichFloat("TargetPerc", 0, "Percentage reduction (0..1)", "If non zero, this parameter specifies the desired final size of the mesh as a percentage of the initial size."));
    result.addParam(RichFloat("QualityThr", quality_threshold, "Quality threshold", "Quality threshold for penalizing bad shaped faces.<br>The value is in the range [0..1]\n 0 accept any kind of face (no penalties),\n 0.5  penalize faces with quality < 0.5, proportionally to their shape\n"));
    result.addParam(RichBool("PreserveBoundary", true, "Preserve Boundary of the mesh", "The simplification process tries to do not affect mesh boundaries during simplification"));
    result.addParam(RichFloat("BoundaryWeight", 1.0, "Boundary Preserving Weight", "The importance of the boundary during simplification. Default (1.0) means that the boundary has the same importance of the rest. Values greater than 1.0 raise boundary importance and has the effect of removing less vertices on the border. Admitted range of values (0,+inf). "));
    result.addParam(RichBool("PreserveNormal", false, "Preserve Normal", "Try to avoid face flipping effects and try to preserve the original orientation of the surface"));
    result.addParam(RichBool("PreserveTopology", false, "Preserve Topology", "Avoid all the collapses that should cause a topology change in the mesh (like closing holes, squeezing handles, etc). If checked the genus of the mesh should stay unchanged."));
    result.addParam(RichBool("OptimalPlacement", true, "Optimal position of simplified vertices", "Each collapsed vertex is placed in the position minimizing the quadric error.\n It can fail (creating bad spikes) in case of very flat areas. \nIf disabled edges are collapsed onto one of the two original vertices and the final mesh is composed by a subset of the original vertices. "));
    result.addParam(RichBool("PlanarQuadric", false, "Planar Simplification", "Add additional simplification constraints that improves the quality of the simplification of the planar portion of the mesh, as a side effect, more triangles will be preserved in flat areas (allowing better shaped triangles)."));
    result.addParam(RichFloat("PlanarWeight", 0.001, "Planar Simp. Weight", "How much we should try to preserve the triangles in the planar regions. If you lower this value planar areas will be simplified more."));
    result.addParam(RichBool("QualityWeight", false, "Weighted Simplification", "Use the Per-Vertex quality as a weighting factor for the simplification. The weight is used as a error amplification value, so a vertex with a high quality value will not be simplified and a portion of the mesh with low quality values will be aggressively simplified."));
    result.addParam(RichBool("AutoClean", true, "Post-simplification cleaning", "After the simplification an additional set of steps is performed to clean the mesh (unreferenced vertices, bad faces, etc)"));
    result.addParam(RichBool("Selected", mesh_model.cm.sfn > 0, "Simplify only selected faces", "The simplification is applied only to the selected set of faces.\n Take care of the target number of faces!"));

	return result;
}

bool filter_call_back(const int pos, const char* str)
{
    return true;
}

bool simplify(MeshDocument& mesh_document, const QAction* p_action, RichParameterList& parameters)
{
    FilterPlugin* p_filter_plugin = qobject_cast<FilterPlugin*>(p_action->parent());
    p_filter_plugin->setLog(&mesh_document.Log);

    try
    {
        mesh_document.meshDocStateData().clear();
        mesh_document.meshDocStateData().create(mesh_document);
    	
        unsigned int post_condition_mask = MeshModel::MM_UNKNOWN;
        p_filter_plugin->applyFilter(p_action, parameters, mesh_document, post_condition_mask, filter_call_back);

        mesh_document.meshDocStateData().clear();

    	return true;
    }
    catch (const std::bad_alloc& exception)
    {
        return false;
    }
    catch (const MLException& exception)
    {
        return false;
    }
}

void load_plugins(std::string executable_path, MeshLabApplication& app, PluginManager& plugin_manager)
{

    auto plugin_directory_path = weakly_canonical(std::filesystem::path(executable_path)).parent_path();

#ifdef NDEBUG
    plugin_directory_path /= "..\\..\\distributions\\release\\plugins";
#else
    plugin_directory_path /= "..\\..\\distributions\\debug\\plugins";
#endif

    plugin_directory_path = std::filesystem::absolute(plugin_directory_path);

    try
    {
        QDir plugin_directory_as_qdir(QString::fromUtf8((plugin_directory_path.generic_string().c_str())));
        auto aa = plugin_directory_as_qdir.absolutePath().toStdString();

        app.addLibraryPath(plugin_directory_as_qdir.absolutePath());
        plugin_manager.loadPlugins(plugin_directory_as_qdir);
    }
    catch (const MLException& e)
    {

    }
	
}

int main(int argc, char *argv[])
{
    //if (argc == 1)
    //{
    //    commandline::usage();

    //    exit(-1);
    //}
	
    MeshLabApplication app(argc, argv);
    PluginManager& plugin_manager = meshlab::pluginManagerInstance();
	
    load_plugins(argv[0], app, plugin_manager);
	
    std::string path_environment_variable = getenv("PATH");
	
    QStringList arguments = app.arguments();
    std::filesystem::path root_source_model_directory_path = "E:\\\\maps\\3d\\sample";
    std::filesystem::path root_target_model_directory_path = "E:\\\\maps\\3d\\sample_1";
    int texture_quality = 50;
	
    std::setlocale(LC_ALL, "C");
    QLocale::setDefault(QLocale::C);

    std::string source_model_file_extension(".3ds");
    QAction* p_filter_action = plugin_manager.filterAction("Simplification: Quadric Edge Collapse Decimation");

	std::filesystem::create_directories(root_target_model_directory_path);
	
    std::filesystem::recursive_directory_iterator source_model_iterator(root_source_model_directory_path);
    for (const auto& entry : source_model_iterator)
    {
        if (std::filesystem::is_directory(entry))
        {
            continue;
        }

        std::filesystem::path input_file_path = entry.path();
        if (input_file_path.extension() != source_model_file_extension)
        {
            continue;
        }
        QString input_file_path_as_qstring = QString::fromUtf8(input_file_path.generic_string().c_str());
    	
    	std::filesystem::path relative_file_path = std::filesystem::relative(input_file_path, root_source_model_directory_path);

        MeshDocument mesh_document;
        if(!import_mesh(input_file_path_as_qstring, plugin_manager, mesh_document))
        {
	        continue;
        }

        std::filesystem::path output_file_path = root_target_model_directory_path / relative_file_path;
        std::filesystem::path output_directory_path = output_file_path.parent_path();
        std::filesystem::create_directories(output_directory_path);

        auto obj_file_path = output_file_path.replace_extension(".obj");
        QString output_file_path_as_qstring = QString::fromUtf8(obj_file_path.generic_string().c_str());

    	MeshModel* p_mesh_model = mesh_document.mm();
        RichParameterList simplification_parameters = build_simplification_parameters(*p_mesh_model, 0.5, 0.3);
        if(!simplify(mesh_document, p_filter_action, simplification_parameters))
        {
			continue;    
        }
    	
    	if(!export_mesh(output_file_path_as_qstring, plugin_manager, mesh_document, texture_quality))
    	{
    		continue;
    	}
    }

	return 0;
}
