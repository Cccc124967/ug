#pragma once
#include <string>

namespace ctx
{

    enum class Coding
    {
        UTF8 ,
        GB2312
    };

    /** 生成项目的行尾风格 */
    enum class LineEnding
    {
        LF ,
        CRLF
    };

    struct GlobalContext
    {
        /** 项目编码(生成的项目,非本项目) */
        Coding coding = Coding::GB2312 ;

        /** 项目行尾(生成的项目,非本项目) */
        LineEnding line_ending = LineEnding::CRLF ;

        /** 项目名 */
        std::string project_name = "NXProject" ;

        /** 项目路径 */
        std::string project_path = {} ;

        /** 是否生成模板 */
        bool generate_template = false ;
    };
}