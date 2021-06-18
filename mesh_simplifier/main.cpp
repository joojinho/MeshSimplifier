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

#include <dimcli/cli.h>

#include <log4cpp/Appender.hh>
#include <log4cpp/Category.hh>
#include <log4cpp/FileAppender.hh>
#include <log4cpp/OstreamAppender.hh>
#include <log4cpp/PatternLayout.hh>

#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QGLFormat>

#include <clocale>
#include <filesystem>
#include <stdlib.h>

bool compare_case_insensitive(std::string& lhs, std::string& rhs)
{
	return ((lhs.size() == rhs.size()) && std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char& c1, char& c2)
	{
		return (c1 == c2 || std::toupper(c1) == std::toupper(c2));
	}));
}

bool export_mesh(QString output_file_path, PluginManager& plugin_manager, MeshDocument& mesh_document,
                 int texture_quality)
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
			std::list<std::string> unloaded_textures = meshlab::loadMesh(
				file_name, p_io_plugin, pre_parameters, mesh_model_ptrs, masks, nullptr);
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

RichParameterList build_simplification_parameters(MeshModel const& mesh_model, float target_face_ratio,
                                                  float quality_threshold)
{
	RichParameterList result;

	result.addParam(RichInt("TargetFaceNum",
	                        (0 < mesh_model.cm.sfn)
		                        ? mesh_model.cm.sfn * target_face_ratio
		                        : mesh_model.cm.fn * target_face_ratio, "Target number of faces",
	                        "The desired final number of faces."));
	result.addParam(RichFloat("TargetPerc", 0, "Percentage reduction (0..1)",
	                          "If non zero, this parameter specifies the desired final size of the mesh as a percentage of the initial size."));
	result.addParam(RichFloat("QualityThr", quality_threshold, "Quality threshold",
	                          "Quality threshold for penalizing bad shaped faces.<br>The value is in the range [0..1]\n 0 accept any kind of face (no penalties),\n 0.5  penalize faces with quality < 0.5, proportionally to their shape\n"));
	result.addParam(RichBool("PreserveBoundary", true, "Preserve Boundary of the mesh",
	                         "The simplification process tries to do not affect mesh boundaries during simplification"));
	result.addParam(RichFloat("BoundaryWeight", 1.0, "Boundary Preserving Weight",
	                          "The importance of the boundary during simplification. Default (1.0) means that the boundary has the same importance of the rest. Values greater than 1.0 raise boundary importance and has the effect of removing less vertices on the border. Admitted range of values (0,+inf). "));
	result.addParam(RichBool("PreserveNormal", false, "Preserve Normal",
	                         "Try to avoid face flipping effects and try to preserve the original orientation of the surface"));
	result.addParam(RichBool("PreserveTopology", false, "Preserve Topology",
	                         "Avoid all the collapses that should cause a topology change in the mesh (like closing holes, squeezing handles, etc). If checked the genus of the mesh should stay unchanged."));
	result.addParam(RichBool("OptimalPlacement", true, "Optimal position of simplified vertices",
	                         "Each collapsed vertex is placed in the position minimizing the quadric error.\n It can fail (creating bad spikes) in case of very flat areas. \nIf disabled edges are collapsed onto one of the two original vertices and the final mesh is composed by a subset of the original vertices. "));
	result.addParam(RichBool("PlanarQuadric", false, "Planar Simplification",
	                         "Add additional simplification constraints that improves the quality of the simplification of the planar portion of the mesh, as a side effect, more triangles will be preserved in flat areas (allowing better shaped triangles)."));
	result.addParam(RichFloat("PlanarWeight", 0.001, "Planar Simp. Weight",
	                          "How much we should try to preserve the triangles in the planar regions. If you lower this value planar areas will be simplified more."));
	result.addParam(RichBool("QualityWeight", false, "Weighted Simplification",
	                         "Use the Per-Vertex quality as a weighting factor for the simplification. The weight is used as a error amplification value, so a vertex with a high quality value will not be simplified and a portion of the mesh with low quality values will be aggressively simplified."));
	result.addParam(RichBool("AutoClean", true, "Post-simplification cleaning",
	                         "After the simplification an additional set of steps is performed to clean the mesh (unreferenced vertices, bad faces, etc)"));
	result.addParam(RichBool("Selected", mesh_model.cm.sfn > 0, "Simplify only selected faces",
	                         "The simplification is applied only to the selected set of faces.\n Take care of the target number of faces!"));

	return result;
}

bool filter_call_back(const int pos, const char* str)
{
	return true;
}

bool simplify(MeshDocument& mesh_document, const QAction* p_filter_action, RichParameterList& parameters)
{
	FilterPlugin* p_filter_plugin = qobject_cast<FilterPlugin*>(p_filter_action->parent());
	p_filter_plugin->setLog(&mesh_document.Log);

	try
	{
		mesh_document.meshDocStateData().clear();
		mesh_document.meshDocStateData().create(mesh_document);

		unsigned int post_condition_mask = MeshModel::MM_UNKNOWN;
		p_filter_plugin->applyFilter(p_filter_action, parameters, mesh_document, post_condition_mask, filter_call_back);

		mesh_document.meshDocStateData().clear();

		return true;
	}
	catch (const std::bad_alloc& exception)
	{
		return false;
	} catch (const MLException& exception)
	{
		return false;
	}
}

std::filesystem::path calculate_plugin_directory_path(std::string executable_path)
{
	auto plugin_directory_path = weakly_canonical(std::filesystem::path(executable_path)).parent_path();

#ifdef NDEBUG
	plugin_directory_path /= "..\\..\\distributions\\release\\plugins";
#else
	plugin_directory_path /= "..\\..\\distributions\\debug\\plugins";
#endif

	return absolute(plugin_directory_path);
}

void load_plugins(std::filesystem::path plugin_directory_path, MeshLabApplication& app, PluginManager& plugin_manager)
{
	try
	{
		const QDir plugin_directory_as_qdir(QString::fromUtf8((plugin_directory_path.generic_string().c_str())));
		
		app.addLibraryPath(plugin_directory_as_qdir.absolutePath());
		plugin_manager.loadPlugins(plugin_directory_as_qdir);
	}
	catch (const MLException& e)
	{
	}
}

int main(int argc, char* argv[])
{
	Dim::Cli cli;

	auto& input_root_directory_path_parameter = cli.opt<std::string>("i").require().desc("input root directory path.").
	                                                check([](auto& cli, auto& opt, auto& val)
	                                                {
		                                                return std::filesystem::exists(*opt) || cli.badUsage(
			                                                "input root directory must exist.");
	                                                });
	auto& output_root_directory_path_parameter = cli.opt<std::string>("o").require().
	                                                 desc("output root directory path.");
	auto& log_file_path_parameter = cli.opt<std::string>("l").require().
		desc("log file path.");
	
	auto& source_model_file_extension_parameter = cli.opt<std::string>("e").require().desc(
		"source model file extension.").check([](auto& cli, auto& opt, auto& val)
	{
		const std::string old_value = *opt;
		if (old_value[0] != '.')
		{
			*opt = "." + old_value;
		}

		return true;
	});

	auto& texture_quality_parameter = cli.opt<int>("t", 50).clamp(0, 100).desc("texture quality.");
	auto& mesh_quality_parameter = cli.opt<int>("m", 30).clamp(1, 100).desc("mesh quality.");
	auto& target_face_ratio_parameter = cli.opt<int>("f", 30).clamp(1, 100).desc("target face ratio.");

	if (!cli.parse(argc, argv))
	{
		return cli.printError(std::cerr);
	}

	log4cpp::Category& category = log4cpp::Category::getInstance("main");
	category.setPriority(log4cpp::Priority::INFO);

	{
		std::filesystem::path log_file_path = *log_file_path_parameter;
		log4cpp::Appender* appender = new log4cpp::FileAppender("RollingFileAppender", log_file_path.generic_string());//The first parameter is the name of appender, and the second is the name of the log file.

		auto layout = new log4cpp::PatternLayout();
		layout->setConversionPattern("[%p]%d{%d %b %Y %H:%M:%S.%l} %m %n");
		appender->setLayout(layout);
		category.addAppender(appender);
	}
	{
		log4cpp::Appender* appender = new log4cpp::OstreamAppender("ConsoleAppender", &std::cout);
		auto layout = new log4cpp::PatternLayout();
		layout->setConversionPattern("[%p]%d{%d %b %Y %H:%M:%S.%l} %m %n");
		appender->setLayout(layout);
		category.addAppender(appender);
	}

	{
		std::vector<std::string> all_args;
		all_args.assign(argv + 1, argv + argc);

		std::ostringstream imploded;
		std::copy(all_args.begin(), all_args.end(), std::ostream_iterator<std::string>(imploded, " "));

		std::string message = "program arguments : ";
		message += imploded.str();

		category.info(message);
	}
	
	std::filesystem::path root_source_model_directory_path = *input_root_directory_path_parameter;
	std::filesystem::path root_target_model_directory_path = *output_root_directory_path_parameter;
	std::string source_model_file_extension = *source_model_file_extension_parameter;

	int texture_quality = *texture_quality_parameter;
	float mesh_quality = *mesh_quality_parameter / 100.0f;
	float target_face_ratio = *target_face_ratio_parameter / 100.0f;

	MeshLabApplication app(argc, argv);
	std::setlocale(LC_ALL, "C");
	QLocale::setDefault(QLocale::C);

	PluginManager& plugin_manager = meshlab::pluginManagerInstance();
	std::filesystem::path plugin_directory_path = calculate_plugin_directory_path(argv[0]);

	{
		std::string message = "loading plugins starts : ";
		message += plugin_directory_path.generic_string();

		category.info(message);
	}

	load_plugins(plugin_directory_path, app, plugin_manager);

	{
		std::string message = "loading plugins ends : ";
		message += plugin_directory_path.generic_string();

		category.info(message);
	}
	
	QAction* p_filter_action = plugin_manager.filterAction("Simplification: Quadric Edge Collapse Decimation");

	if (exists(root_target_model_directory_path))
	{
		remove_all(root_target_model_directory_path);
	}
	create_directories(root_target_model_directory_path);


	{
		std::string message = "simplifying starts";

		category.info(message);
	}
	
	long success_count = 0;
	long fail_count = 0;

	std::filesystem::recursive_directory_iterator source_model_iterator(root_source_model_directory_path);
	for (const auto& entry : source_model_iterator)
	{
		if (is_directory(entry))
		{
			continue;
		}

		std::filesystem::path input_file_path = entry.path();
		std::string input_file_extension = input_file_path.extension().string();
		if (!compare_case_insensitive(input_file_extension, source_model_file_extension))
		{
			continue;
		}
		QString input_file_path_as_qstring = QString::fromUtf8(input_file_path.generic_string().c_str());

		MeshDocument mesh_document;
		if (!import_mesh(input_file_path_as_qstring, plugin_manager, mesh_document))
		{
			++fail_count;
			
			std::string message = "simplification fail";
			message += "(" + std::to_string(fail_count) + "/" + std::to_string(success_count)+ ")";
			message += " - import error : ";
			message += input_file_path.generic_string();

			category.warn(message);

			continue;
		}

		MeshModel* p_mesh_model = mesh_document.mm();
		RichParameterList simplification_parameters = build_simplification_parameters(
			*p_mesh_model, target_face_ratio, mesh_quality);
		if (!simplify(mesh_document, p_filter_action, simplification_parameters))
		{
			++fail_count;

			std::string message = "simplification fail";
			message += "(" + std::to_string(fail_count) + "/" + std::to_string(success_count) + ")";
			message += " - simplification error : ";
			message += input_file_path.generic_string();

			category.warn(message);

			continue;
		}

		std::filesystem::path relative_file_path = relative(input_file_path, root_source_model_directory_path);
		std::filesystem::path output_file_path = root_target_model_directory_path / relative_file_path;
		std::filesystem::path output_directory_path = output_file_path.parent_path();
		create_directories(output_directory_path);

		auto obj_file_path = output_file_path.replace_extension(".obj");
		QString output_file_path_as_qstring = QString::fromUtf8(obj_file_path.generic_string().c_str());

		if (!export_mesh(output_file_path_as_qstring, plugin_manager, mesh_document, texture_quality))
		{
			++fail_count;

			std::string message = "simplification fail";
			message += "(" + std::to_string(fail_count) + "/" + std::to_string(success_count) + ")";
			message += " - export error : ";
			message += input_file_path.generic_string();

			category.warn(message);
		}
		else
		{
			++success_count;

			std::string message = "simplification success";
			message += "(" + std::to_string(fail_count) + "/" + std::to_string(success_count) + ") : ";
			message += input_file_path.generic_string();
			message += " => ";
			message += output_file_path.generic_string();

			category.info(message);
		}
		
	}

	{
		std::string message = "simplifying ends";

		category.info(message);
	}
	
	category.shutdown();
	
	return 0;
}
