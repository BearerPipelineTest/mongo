/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <fmt/format.h>

#include "mongo/db/exec/projection_executor_utils.h"
#include "mongo/db/matcher/copyable_match_expression.h"
#include "mongo/db/pipeline/expression.h"

namespace mongo {
/**
 * An internal expression to apply a find positional projection to the projection post image
 * document. This expression is never parsed or serialized.
 *
 * See projection_executor::applyPositionalProjection() for more details.
 */
class ExpressionInternalFindPositional final : public Expression {
public:
    ExpressionInternalFindPositional(ExpressionContext* const expCtx,
                                     boost::intrusive_ptr<Expression> preImageExpr,
                                     boost::intrusive_ptr<Expression> postImageExpr,
                                     FieldPath path,
                                     CopyableMatchExpression matchExpr)
        : Expression{expCtx, {preImageExpr, postImageExpr}},
          _path{std::move(path)},
          _matchExpr{std::move(matchExpr)} {}

    Value evaluate(const Document& root, Variables* variables) const final {
        using namespace fmt::literals;

        auto preImage = _children[0]->evaluate(root, variables);
        auto postImage = _children[1]->evaluate(root, variables);
        uassert(51255,
                "Positional operator pre-image can only be an object, but got {}"_format(
                    typeName(preImage.getType())),
                preImage.getType() == BSONType::Object);
        uassert(51258,
                "Positional operator post-image can only be an object, but got {}"_format(
                    typeName(postImage.getType())),
                postImage.getType() == BSONType::Object);
        return Value{projection_executor_utils::applyFindPositionalProjection(
            preImage.getDocument(), postImage.getDocument(), *_matchExpr, _path)};
    }

    void acceptVisitor(ExpressionMutableVisitor* visitor) final {
        return visitor->visit(this);
    }

    void acceptVisitor(ExpressionConstVisitor* visitor) const final {
        return visitor->visit(this);
    }

    Value serialize(bool explain) const final {
        MONGO_UNREACHABLE;
    }

    ComputedPaths getComputedPaths(const std::string& exprFieldPath,
                                   Variables::Id renamingVar = Variables::kRootId) const final {
        // The positional projection can change any field within the path it's applied to, so we'll
        // treat the first component in '_positionalInfo.path' as the computed path.
        return {{_path.front().toString()}, {}};
    }

    boost::intrusive_ptr<Expression> optimize() final {
        for (const auto& child : _children) {
            child->optimize();
        }
        // SERVER-43740: ideally we'd want to optimize '_matchExpr' here as well. However, given
        // that the match expression is stored as a shared copyable expression in this class, and
        // 'MatchExpression::optimize()' takes and returns a unique pointer on a match expression,
        // there is no easy way to replace a copyable match expression with its optimized
        // equivalent. So, for now we will assume that the copyable match expression is passed to
        // this expression already optimized. Once we have MatchExpression and Expression combined,
        // we should revisit this code and make sure that 'optimized()' method is called on
        // _matchExpr.
        return this;
    }

    const CopyableMatchExpression& getMatchExpr() const {
        return _matchExpr;
    }

private:
    const FieldPath _path;
    const CopyableMatchExpression _matchExpr;
};

/**
 * An internal expression to apply a find $slice projection to the projection post image document.
 * This expression is never parsed or serialized.
 *
 * See projection_executor::applySliceProjection() for more details.
 */
class ExpressionInternalFindSlice final : public Expression {
public:
    ExpressionInternalFindSlice(ExpressionContext* const expCtx,
                                boost::intrusive_ptr<Expression> postImageExpr,
                                FieldPath path,
                                boost::optional<int> skip,
                                int limit)
        : Expression{expCtx, {postImageExpr}}, _path{std::move(path)}, _skip{skip}, _limit{limit} {}

    Value evaluate(const Document& root, Variables* variables) const final {
        using namespace fmt::literals;

        auto postImage = _children[0]->evaluate(root, variables);
        uassert(51256,
                "$slice operator can only be applied to an object, but got {}"_format(
                    typeName(postImage.getType())),
                postImage.getType() == BSONType::Object);
        return Value{projection_executor_utils::applyFindSliceProjection(
            postImage.getDocument(), _path, _skip, _limit)};
    }

    void acceptVisitor(ExpressionMutableVisitor* visitor) final {
        return visitor->visit(this);
    }

    void acceptVisitor(ExpressionConstVisitor* visitor) const final {
        return visitor->visit(this);
    }

    Value serialize(bool explain) const final {
        MONGO_UNREACHABLE;
    }

    ComputedPaths getComputedPaths(const std::string& exprFieldPath,
                                   Variables::Id renamingVar = Variables::kRootId) const final {
        // The $slice projection can change any field within the path it's applied to, so we'll
        // treat the first component in '_sliceInfo.path' as the computed path.
        return {{_path.front().toString()}, {}};
    }

    boost::intrusive_ptr<Expression> optimize() final {
        invariant(_children.size() == 1ul);

        _children[0]->optimize();
        return this;
    }

private:
    const FieldPath _path;
    const boost::optional<int> _skip;
    const int _limit;
};

/**
 * An internal expression to apply a find $elemMatch projection to the document root.
 * See projection_executor::applyElemMatchProjection() for details.
 */
class ExpressionInternalFindElemMatch final : public Expression {
public:
    ExpressionInternalFindElemMatch(ExpressionContext* const expCtx,
                                    boost::intrusive_ptr<Expression> child,
                                    FieldPath path,
                                    CopyableMatchExpression matchExpr)
        : Expression{expCtx, {child}}, _path{std::move(path)}, _matchExpr{std::move(matchExpr)} {}

    Value evaluate(const Document& root, Variables* variables) const final {
        using namespace fmt::literals;

        auto input = _children[0]->evaluate(root, variables);
        invariant(input.getType() == BSONType::Object);
        return projection_executor_utils::applyFindElemMatchProjection(
            input.getDocument(), *_matchExpr, _path);
    }

    void acceptVisitor(ExpressionMutableVisitor* visitor) final {
        return visitor->visit(this);
    }

    void acceptVisitor(ExpressionConstVisitor* visitor) const final {
        return visitor->visit(this);
    }

    Value serialize(bool explain) const final {
        MONGO_UNREACHABLE;
    }

    boost::intrusive_ptr<Expression> optimize() final {
        invariant(_children.size() == 1ul);

        _children[0]->optimize();
        // SERVER-43740: ideally we'd want to optimize '_matchExpr' here as well. However, given
        // that the match expression is stored as a shared copyable expression in this class, and
        // 'MatchExpression::optimize()' takes and returns a unique pointer on a match expression,
        // there is no easy way to replace a copyable match expression with its optimized
        // equivalent. So, for now we will assume that the copyable match expression is passed to
        // this expression already optimized. Once we have MatchExpression and Expression combined,
        // we should revisit this code and make sure that 'optimized()' method is called on
        // _matchExpr.
        return this;
    }

    const CopyableMatchExpression& getMatchExpr() const {
        return _matchExpr;
    }

private:
    const FieldPath _path;
    const CopyableMatchExpression _matchExpr;
};
}  // namespace mongo
